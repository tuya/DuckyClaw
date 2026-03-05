/**
 * @file agent_loop.c
 * @brief agent_loop module is used to 
 * @version 0.1
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
#include "tool_files.h"
/***********************************************************
************************macro define************************
***********************************************************/
#define DUCKY_CLAW_AGENT_STACK   (4*1024)

#define DUCKY_CLAW_CONTEXT_BUF_SIZE        (16 * 1024)

#define DUCKY_CLAW_HISTORY_MAX_COUNT 10

/***********************************************************
***********************typedef define***********************
***********************************************************/


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

/***********************************************************
***********************function define**********************
***********************************************************/
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

    // get history json item count
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

static void agent_loop_task(void *arg)
{
    PR_DEBUG("Agent loop task started");
    while (1) {
        tal_system_sleep(100);

        im_msg_t in = {0};
        if (message_bus_pop_inbound(&in, UINT32_MAX) != OPRT_OK) continue;
        if (!in.content) continue;
        if (!s_total_prompt) continue;
        app_im_set_chat_id(in.chat_id);

        PR_DEBUG("Agent loop task running: channel=%s, chat_id=%s, content=%s", in.channel, in.chat_id, in.content ? in.content : "");

        /* 1. Build system prompt */
        memset(s_total_prompt, 0, DUCKY_CLAW_CONTEXT_BUF_SIZE);
        size_t system_prompt_len = context_build_system_prompt(s_total_prompt, DUCKY_CLAW_CONTEXT_BUF_SIZE);
        if (system_prompt_len == 0) {
            PR_ERR("context_build_system_prompt failed");
            continue;
        }

        /* 2. Build history */
        int history_count = cJSON_GetArraySize(s_history_json);
        if (history_count > 0) {
            // ## Recent  memory history
            system_prompt_len += snprintf(s_total_prompt + system_prompt_len, DUCKY_CLAW_CONTEXT_BUF_SIZE - system_prompt_len, "\r\n\n# Recent  Memory history\r\n");
            for (int j = 0; j < history_count; j++) {
                cJSON *history_json = cJSON_GetArrayItem(s_history_json, j);
                if (!history_json) {
                    continue;
                }
                const char *role = cJSON_GetStringValue(cJSON_GetObjectItem(history_json, "role"));
                const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(history_json, "content"));
                system_prompt_len += snprintf(s_total_prompt + system_prompt_len, DUCKY_CLAW_CONTEXT_BUF_SIZE - system_prompt_len, "\n- %s: %s", role, content);
            }
        }

        /* 3. Add user messages to system prompt */
        if (system_prompt_len + strlen(in.content) + strlen("\n\n# User content\r\n") > DUCKY_CLAW_CONTEXT_BUF_SIZE) {
            PR_ERR("system prompt too long");
            continue;
        }
        system_prompt_len += snprintf(s_total_prompt + system_prompt_len, DUCKY_CLAW_CONTEXT_BUF_SIZE - system_prompt_len, "\n\n# User content\r\n%s", in.content);

        PR_DEBUG("system prompt: %s", s_total_prompt);

        // Step x: upload to tuya agent
        ai_agent_send_text(s_total_prompt);

        // add user context to history
        build_current_context("user", in.content);

        claw_free(in.content);
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
    in.content = claw_malloc(strlen(GREETING_MESSAGE) + 1);
    if (!in.content) {
        return OPRT_MALLOC_FAILED;
    }
    memset(in.content, 0, strlen(GREETING_MESSAGE) + 1);
    strncpy(in.content, GREETING_MESSAGE, strlen(GREETING_MESSAGE) + 1);
    message_bus_push_inbound(&in);

    return 0;
}

OPERATE_RET agent_loop_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    if (s_agent_loop_thread) {
        return OPRT_OK;
    }

    if (!s_total_prompt) {
        s_total_prompt = claw_malloc(DUCKY_CLAW_CONTEXT_BUF_SIZE);
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

    PR_DEBUG("Agent loop initialized");

    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth = DUCKY_CLAW_AGENT_STACK;
    thrd_param.priority = THREAD_PRIO_1;
    thrd_param.thrdname = "agent_loop";
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    thrd_param.psram_mode = 1;
#endif
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&s_agent_loop_thread, NULL, NULL, agent_loop_task, NULL, &thrd_param));

    // tal_event_subscribe(EVENT_AI_CLIENT_RUN, "agent_loop", agent_loop_start_cb, SUBSCRIBE_TYPE_NORMAL);

    return OPRT_OK;
}

