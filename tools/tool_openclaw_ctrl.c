/**
 * @file tool_openclaw_ctrl.c
 * @brief MCP tools: openclaw_ctrl, tuyaclaw_ctrl, pc_ctrl (shared ACP inject logic).
 *
 * @version 2.1
 * @date 2026-04-20
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */

#include "tool_openclaw_ctrl.h"

#include "ai_mcp_server.h"
#include "acp_client.h"
#include "tal_log.h"
#include "tal_api.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
static const char s_mcp_ud_openclaw[] = "openclaw_ctrl";
static const char s_mcp_ud_tuyaclaw[] = "tuyaclaw_ctrl";
static const char s_mcp_ud_pc[]       = "pc_ctrl";

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */

/**
 * @brief Retrieve a string property from an MCP property list.
 *
 * @param[in] props  MCP property list.
 * @param[in] name   Property name.
 * @return String value pointer, or NULL if not found.
 */
static const char *__get_str_prop(const MCP_PROPERTY_LIST_T *props,
                                  const char *name)
{
    const MCP_PROPERTY_T *p = ai_mcp_property_list_find(props, name);
    if (p && p->type == MCP_PROPERTY_TYPE_STRING && p->default_val.str_val) {
        return p->default_val.str_val;
    }
    return NULL;
}

/* ---------------------------------------------------------------------------
 * Shared handler (openclaw_ctrl / tuyaclaw_ctrl / pc_ctrl)
 * --------------------------------------------------------------------------- */

/**
 * @brief MCP tool handler: shared logic for gateway PC task notification.
 *
 * @param[in]  properties  MCP property list (message).
 * @param[out] ret_val     Result string returned to the agent.
 * @param[in]  user_data   Registered tool name string for logs (may be NULL).
 * @return OPRT_OK on success.
 */
static OPERATE_RET __tool_openclaw_ctrl(const MCP_PROPERTY_LIST_T *properties,
                                        MCP_RETURN_VALUE_T *ret_val,
                                        void *user_data)
{
    const char *message = __get_str_prop(properties, "message");
    if (!message || message[0] == '\0') {
        PR_WARN("[tool_openclaw/tuyaclaw_ctrl/pc_ctrl] missing 'message' parameter");
        ai_mcp_return_value_set_str(ret_val, "Error: 'message' parameter is required");
        return OPRT_INVALID_PARM;
    }

    /* -----------------------------------------------------------------------
     * Primary path: ACP / WS
     * ----------------------------------------------------------------------- */
    if (acp_client_is_connected()) {
        PR_INFO("[tool_openclaw/tuyaclaw_ctrl/pc_ctrl] ACP connected, sending request");

        OPERATE_RET rt = acp_client_inject(message);
        if (rt == OPRT_OK) {
            ai_mcp_return_value_set_str(ret_val,
                "OpenClaw notification sent successfully. "
                "Do not report the task as completed yet. "
                "Tell the user you have notified OpenClaw and that the execution result "
                "will be reported asynchronously in a follow-up reply.");
            return OPRT_OK;
        }

        PR_WARN("[tool_openclaw/tuyaclaw_ctrl/pc_ctrl] ACP send failed (rt=%d)", rt);
        return rt;
    } else {
        PR_ERR("[tool_openclaw/tuyaclaw_ctrl/pc_ctrl] ACP not connected");
    }

    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Registration
 * --------------------------------------------------------------------------- */

/**
 * @brief Register OpenClaw / TuyaClaw / PC control MCP tools.
 *
 * Registers three tools with identical behavior (ACP inject when connected):
 *   openclaw_ctrl   – OpenClaw gateway assistant on the user's PC
 *   tuyaclaw_ctrl   – same path, TuyaClaw-branded naming for the agent
 *   pc_ctrl         – same path, generic PC task naming
 *
 * @return OPRT_OK on success, error code on failure.
 */
OPERATE_RET tool_openclaw_ctrl_register(void)
{
    OPERATE_RET rt = OPRT_OK;

    PR_INFO("[tool_openclaw/tuyaclaw_ctrl/pc_ctrl] registering tools...");

    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "openclaw_ctrl",
        "Notify the OpenClaw AI assistant (running on the user's PC) to perform "
        "a task on the computer. "
        "OpenClaw is an autonomous AI agent with full access to the PC: it can "
        "create/edit files, run scripts, open applications, search the web, and "
        "execute any desktop operation. "
        "Use this tool whenever the user wants something done on their PC. "
        "Describe the task clearly in 'message'. "
        "If ACP is connected, this tool only sends the request to OpenClaw. "
        "The real execution result will arrive asynchronously later as a new "
        "message, so after a successful call you must not pretend you already "
        "know the outcome. Instead, tell the user that OpenClaw has been "
        "notified and that you are waiting for the result. ",
        __tool_openclaw_ctrl,
        (void *)s_mcp_ud_openclaw,
        MCP_PROP_STR("message",
            "Task description for the OpenClaw AI assistant "
            "(e.g. 'Create a quicksort Python script on the desktop').")
    ));

    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "tuyaclaw_ctrl",
        "Notify the TuyaClaw AI assistant (running on the user's PC) to perform "
        "a task on the computer. "
        "TuyaClaw is an autonomous AI agent with full access to the PC: it can "
        "create/edit files, run scripts, open applications, search the web, and "
        "execute any desktop operation. "
        "Use this tool whenever the user wants something done on their PC. "
        "Describe the task clearly in 'message'. "
        "If ACP is connected, this tool only sends the request to TuyaClaw. "
        "The real execution result will arrive asynchronously later as a new "
        "message, so after a successful call you must not pretend you already "
        "know the outcome. Instead, tell the user that TuyaClaw has been "
        "notified and that you are waiting for the result. ",
        __tool_openclaw_ctrl,
        (void *)s_mcp_ud_tuyaclaw,
        MCP_PROP_STR("message",
            "Task description for the TuyaClaw AI assistant.")
    ));

    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "pc_ctrl",
        "Ask the PC-side AI assistant (OpenClaw / TuyaClaw) to perform a task on the user's computer. "
        "The PC-side AI assistant is an autonomous AI agent with full access to the PC: it can "
        "create/edit files, run scripts, open applications, search the web, and "
        "execute any desktop operation. "
        "Use this tool whenever the user wants something done on their PC. "
        "Describe the task clearly in 'message'. "
        "If ACP is connected, this tool only sends the request to the PC-side AI assistant. "
        "The real execution result will arrive asynchronously later as a new "
        "message, so after a successful call you must not pretend you already "
        "know the outcome. Instead, tell the user that the PC-side AI assistant has been "
        "notified and that you are waiting for the result. ",
        __tool_openclaw_ctrl,
        (void *)s_mcp_ud_pc,
        MCP_PROP_STR("message", "Task description for the PC-side AI assistant.")
    ));

    PR_INFO("[tool_openclaw/tuyaclaw_ctrl/pc_ctrl] registration done");
    return rt;
}
