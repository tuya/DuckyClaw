/**
 * @file agent_loop.c
 * @brief Agent main loop: builds context, dispatches to AI, handles tool retry.
 * @version 0.2
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "agent_loop.h"
#include "context_builder.h"

#include "cJSON.h"
#include "tal_api.h"
#include "tal_mutex.h"
#include "tuya_ai_client.h"
#include <stdint.h>

#include "im_api.h"
#include "app_im.h"
#include "ai_agent.h"
#include "ai_chat_main.h"
#include "tuya_cloud_types.h"
#include "tuya_error_code.h"
#include "ai_mcp_server.h"

/***********************************************************
************************macro define************************
***********************************************************/
#ifndef DUCKY_CLAW_AGENT_STACK
#define DUCKY_CLAW_AGENT_STACK   (24*1024)
#endif

#ifndef DUCKY_CLAW_CONTEXT_BUF_SIZE
#define DUCKY_CLAW_CONTEXT_BUF_SIZE   (32 * 1024)
#endif

#ifndef DUCKY_CLAW_HISTORY_MAX_COUNT
#define DUCKY_CLAW_HISTORY_MAX_COUNT  10
#endif

/* Channel name used to identify automatic tool-retry messages in the loop */
#define TOOL_RETRY_CHANNEL  "tool_retry"

/* Maximum number of automatic retries per conversation turn */
#define TOOL_RETRY_MAX      10

/***********************************************************
***********************typedef define***********************
***********************************************************/

/** Tracks per-turn tool retry state. */
typedef struct {
    int          count;  /* Retry attempts consumed this turn */
    MUTEX_HANDLE lock;
} tool_retry_ctx_t;

/***********************************************************
********************function declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
static THREAD_HANDLE s_agent_loop_thread = NULL;

static char *s_total_prompt = NULL;

static MUTEX_HANDLE s_history_mutex = NULL;
static cJSON *s_history_json = NULL;

static tool_retry_ctx_t s_retry_ctx = {0};

/***********************************************************
***********************function define**********************
***********************************************************/

/**
 * __oprt_to_str - Map an OPERATE_RET code to a human-readable string.
 * @rt: Error code to map.
 *
 * Return: A constant string describing the error, or "unknown error".
 */
static const char *__oprt_to_str(OPERATE_RET rt)
{
    switch (rt) {
    case OPRT_OK:                       return "success";
    case OPRT_COM_ERROR:                return "common error";
    case OPRT_INVALID_PARM:             return "invalid parameter";
    case OPRT_MALLOC_FAILED:            return "memory allocation failed";
    case OPRT_NOT_SUPPORTED:            return "not supported";
    case OPRT_NETWORK_ERROR:            return "network error";
    case OPRT_NOT_FOUND:                return "not found";
    case OPRT_TIMEOUT:                  return "timeout";
    case OPRT_FILE_NOT_FIND:            return "file not found";
    case OPRT_FILE_OPEN_FAILED:         return "file open failed";
    case OPRT_FILE_READ_FAILED:         return "file read failed";
    case OPRT_FILE_WRITE_FAILED:        return "file write failed";
    case OPRT_NOT_EXIST:                return "not exist";
    case OPRT_BUFFER_NOT_ENOUGH:        return "buffer not enough";
    case OPRT_RESOURCE_NOT_READY:       return "resource not ready";
    case OPRT_EXCEED_UPPER_LIMIT:       return "exceed upper limit";
    default:                            return "unknown error";
    }
}

/**
 * __push_tool_retry_msg - Push an automatic retry message into the inbound queue.
 * @reason: Formatted failure description from the hook callback.
 *
 * Injects a "tool_retry" channel message so agent_loop_task picks it up on the
 * next iteration and re-sends the context (with failure history) to the AI.
 */
static void __push_tool_retry_msg(const char *reason)
{
    if (!reason) {
        return;
    }

    const char *prefix = "Previous tool call failed, please analyze and retry. Reason: ";
    size_t len = strlen(prefix) + strlen(reason) + 1;

    im_msg_t retry_msg = {0};
    strncpy(retry_msg.channel, TOOL_RETRY_CHANNEL, sizeof(retry_msg.channel) - 1);

#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    retry_msg.content = tal_psram_malloc(len);
#else
    retry_msg.content = tal_malloc(len);
#endif
    if (!retry_msg.content) {
        PR_ERR("Failed to alloc retry message buffer");
        return;
    }

    snprintf(retry_msg.content, len, "%s%s", prefix, reason);
    message_bus_push_inbound(&retry_msg);
    PR_INFO("Tool retry message queued (attempt %d/%d)", s_retry_ctx.count, TOOL_RETRY_MAX);
}

/**
 * __on_tool_executed - MCP tool execution hook called after every tool call.
 * @tool_name: Name of the executed tool.
 * @rt:        Return code from the tool callback.
 * @ret_val:   Tool return value (valid only when rt == OPRT_OK, freed after return).
 * @user_data: Unused.
 *
 * Formats a one-line summary of the tool result, appends it to the conversation
 * history, and – on failure – automatically pushes a retry message into the
 * inbound queue up to TOOL_RETRY_MAX times per conversation turn.
 */
static void __on_tool_executed(const char *tool_name, OPERATE_RET rt,
                                const MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
#define TOOL_RESULT_BUF_SIZE 512

    char buf[TOOL_RESULT_BUF_SIZE];
    int  offset = 0;

    if (!tool_name) {
        return;
    }

    offset += snprintf(buf + offset, TOOL_RESULT_BUF_SIZE - offset,
                       "Tool \"%s\" executed: %s (code=%d)",
                       tool_name, __oprt_to_str(rt), rt);

    if (rt == OPRT_OK && ret_val) {
        switch (ret_val->type) {
        case MCP_RETURN_TYPE_BOOLEAN:
            snprintf(buf + offset, TOOL_RESULT_BUF_SIZE - offset,
                     ", result=%s", ret_val->bool_val ? "true" : "false");
            break;
        case MCP_RETURN_TYPE_INTEGER:
            snprintf(buf + offset, TOOL_RESULT_BUF_SIZE - offset,
                     ", result=%d", ret_val->int_val);
            break;
        case MCP_RETURN_TYPE_STRING:
            if (ret_val->str_val) {
                snprintf(buf + offset, TOOL_RESULT_BUF_SIZE - offset,
                         ", result=%.200s", ret_val->str_val);
            }
            break;
        case MCP_RETURN_TYPE_JSON:
            if (ret_val->json_val) {
                char *s = cJSON_PrintUnformatted(ret_val->json_val);
                if (s) {
                    snprintf(buf + offset, TOOL_RESULT_BUF_SIZE - offset,
                             ", result=%.200s", s);
                    cJSON_free(s);
                }
            }
            break;
        default:
            break;
        }
    }

    PR_DEBUG("Tool exec hook: %s", buf);

    /* Always write the result into conversation history */
    build_current_context("tool", buf);

    /* On failure: push an automatic retry message if under the per-turn limit */
    if (rt != OPRT_OK && s_retry_ctx.lock) {
        tal_mutex_lock(s_retry_ctx.lock);
        if (s_retry_ctx.count < TOOL_RETRY_MAX) {
            s_retry_ctx.count++;
            tal_mutex_unlock(s_retry_ctx.lock);
            __push_tool_retry_msg(buf);
        } else {
            tal_mutex_unlock(s_retry_ctx.lock);
            PR_WARN("Tool retry limit reached (%d/%d), skipping auto-retry",
                    TOOL_RETRY_MAX, TOOL_RETRY_MAX);
        }
    }

#undef TOOL_RESULT_BUF_SIZE
}

/**
 * build_current_context - Append a role/content pair to the shared history array.
 * @role:    Sender role string (e.g. "user", "assistant", "tool").
 * @content: Message content.
 *
 * Trims the oldest entry when the history exceeds DUCKY_CLAW_HISTORY_MAX_COUNT.
 * Thread-safe via s_history_mutex.
 *
 * Return: OPRT_OK on success, error code otherwise.
 */
OPERATE_RET build_current_context(const char *role, const char *content)
{
    OPERATE_RET rt = OPRT_OK;

    if (s_history_mutex == NULL || s_history_json == NULL) {
        return OPRT_INVALID_PARM;
    }

    tal_mutex_lock(s_history_mutex);

    cJSON *current_json = cJSON_CreateObject();
    if (!current_json) {
        tal_mutex_unlock(s_history_mutex);
        return OPRT_MALLOC_FAILED;
    }

    /* Drop the oldest entry to stay within the sliding window */
    int history_count = cJSON_GetArraySize(s_history_json);
    if (history_count >= DUCKY_CLAW_HISTORY_MAX_COUNT) {
        cJSON_DeleteItemFromArray(s_history_json, 0);
    }

    cJSON_AddStringToObject(current_json, "role", role);
    cJSON_AddStringToObject(current_json, "content", content);

    cJSON_AddItemToArray(s_history_json, current_json);

    tal_mutex_unlock(s_history_mutex);

    return rt;
}

/**
 * agent_loop_task - Main agent loop thread function.
 *
 * Blocks on the inbound message queue.  For each message:
 *   1. Resets the per-turn retry counter when the message is user-initiated
 *      (channel != TOOL_RETRY_CHANNEL).
 *   2. Builds a system prompt from context, sliding-window history, and the
 *      incoming message content.
 *   3. Sends the prompt to the cloud AI via ai_agent_send_text().
 *   4. Records the user/retry message in history.
 *
 * When a tool fails, __on_tool_executed() pushes a TOOL_RETRY_CHANNEL message
 * back into the queue (up to TOOL_RETRY_MAX times), causing this loop to
 * re-send with the failure reason included in the history.
 */
static void agent_loop_task(void *arg)
{
    PR_DEBUG("Agent loop task started");
    while (1) {
        im_msg_t in = {0};
        if (message_bus_pop_inbound(&in, UINT32_MAX) != OPRT_OK) {
            continue;
        }
        if (!in.content) {
            continue;
        }
        if (!s_total_prompt) {
            tal_free(in.content);
            continue;
        }

        /* Preserve the original chat_id across tool retries.
         * Tool retry messages carry an empty chat_id; updating s_chat_id
         * with an empty string would cause subsequent replies to be dropped. */
        if (in.chat_id[0] != '\0') {
            app_im_set_chat_id(in.chat_id);
        }

        PR_DEBUG("Agent loop: channel=%s chat_id=%s content=%.80s",
                 in.channel, in.chat_id, in.content);

        /* Reset the retry counter for every new user-initiated turn */
        bool is_retry = (strcmp(in.channel, TOOL_RETRY_CHANNEL) == 0);
        if (!is_retry && s_retry_ctx.lock) {
            tal_mutex_lock(s_retry_ctx.lock);
            s_retry_ctx.count = 0;
            tal_mutex_unlock(s_retry_ctx.lock);
        }

        /* 1. Build base system prompt */
        memset(s_total_prompt, 0, DUCKY_CLAW_CONTEXT_BUF_SIZE);
        size_t system_prompt_len = context_build_system_prompt(s_total_prompt,
                                                               DUCKY_CLAW_CONTEXT_BUF_SIZE);
        if (system_prompt_len == 0) {
            PR_ERR("context_build_system_prompt failed");
            tal_free(in.content);
            continue;
        }

        /* 2. Append sliding-window history (includes tool results and retries) */
        tal_mutex_lock(s_history_mutex);
        int history_count = cJSON_GetArraySize(s_history_json);
        if (history_count > 0) {
            system_prompt_len += snprintf(
                s_total_prompt + system_prompt_len,
                DUCKY_CLAW_CONTEXT_BUF_SIZE - system_prompt_len,
                "\r\n\n# Recent Memory History\r\n");

            for (int j = 0; j < history_count; j++) {
                cJSON *entry = cJSON_GetArrayItem(s_history_json, j);
                if (!entry) {
                    continue;
                }
                const char *role    = cJSON_GetStringValue(cJSON_GetObjectItem(entry, "role"));
                const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(entry, "content"));
                system_prompt_len += snprintf(
                    s_total_prompt + system_prompt_len,
                    DUCKY_CLAW_CONTEXT_BUF_SIZE - system_prompt_len,
                    "\n- %s: %s", role, content);
            }
        }
        tal_mutex_unlock(s_history_mutex);

        /* 3. Append current message */
        const char *section_header = is_retry
            ? "\n\n# Tool Failure – Retry Request\r\n"
            : "\n\n# User Content\r\n";
        size_t needed = strlen(in.content) + strlen(section_header) + 1;
        if (system_prompt_len + needed > DUCKY_CLAW_CONTEXT_BUF_SIZE) {
            PR_ERR("System prompt buffer overflow, dropping message");
            tal_free(in.content);
            continue;
        }
        system_prompt_len += snprintf(
            s_total_prompt + system_prompt_len,
            DUCKY_CLAW_CONTEXT_BUF_SIZE - system_prompt_len,
            "%s%s", section_header, in.content);

        PR_DEBUG("System prompt (len=%u): %.200s ...", (unsigned)system_prompt_len, s_total_prompt);

        /* 4. Send to AI */
        ai_agent_send_text(s_total_prompt);

        /* 5. Record the sent message in history */
        build_current_context(is_retry ? "tool_retry" : "user", in.content);

        tal_free(in.content);
        tal_system_sleep(100);
    }
}

int agent_loop_start_cb(void *data)
{
    (void)data;
    PR_DEBUG("Agent loop started");

    #define GREETING_MESSAGE "Wake up, my friend!(This is a greeting message, reply within 10 characters.)"

    im_msg_t in = {0};
    strncpy(in.channel, "system", sizeof(in.channel) - 1);
    strncpy(in.chat_id, "", sizeof(in.chat_id) - 1);
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    in.content = tal_psram_malloc(strlen(GREETING_MESSAGE) + 1);
#else
    in.content = tal_malloc(strlen(GREETING_MESSAGE) + 1);
#endif
    if (!in.content) {
        return OPRT_MALLOC_FAILED;
    }
    memset(in.content, 0, strlen(GREETING_MESSAGE) + 1);
    strncpy(in.content, GREETING_MESSAGE, strlen(GREETING_MESSAGE) + 1);
    message_bus_push_inbound(&in);

    return 0;
}

/**
 * agent_loop_init - Initialise the agent loop and start its thread.
 *
 * Allocates the prompt buffer, history array, and mutexes.
 * Registers the MCP tool execution hook so tool results are captured.
 *
 * Return: OPRT_OK on success, error code on failure.
 */
OPERATE_RET agent_loop_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    if (s_agent_loop_thread) {
        return OPRT_OK;
    }

    if (!s_total_prompt) {
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
        s_total_prompt = tal_psram_malloc(DUCKY_CLAW_CONTEXT_BUF_SIZE);
#else
        s_total_prompt = tal_malloc(DUCKY_CLAW_CONTEXT_BUF_SIZE);
#endif
        if (!s_total_prompt) {
            return OPRT_MALLOC_FAILED;
        }
        memset(s_total_prompt, 0, DUCKY_CLAW_CONTEXT_BUF_SIZE);
    }

    if (!s_history_json) {
        s_history_json = cJSON_CreateArray();
        if (!s_history_json) {
            return OPRT_MALLOC_FAILED;
        }
    }

    if (!s_history_mutex) {
        TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&s_history_mutex));
    }

    if (!s_retry_ctx.lock) {
        TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&s_retry_ctx.lock));
    }

    /* Register hook so every MCP tool result is captured in history */
    ai_mcp_server_set_tool_exec_hook(__on_tool_executed, NULL);

    PR_DEBUG("Agent loop initialized");

    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth = DUCKY_CLAW_AGENT_STACK;
    thrd_param.priority   = THREAD_PRIO_1;
    thrd_param.thrdname   = "agent_loop";
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    thrd_param.psram_mode = 1;
#endif
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&s_agent_loop_thread, NULL, NULL,
                                                      agent_loop_task, NULL, &thrd_param));

    return OPRT_OK;
}
