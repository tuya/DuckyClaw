/**
 * @file tool_openclaw_ctrl.h
 * @brief MCP tools: openclaw_ctrl, tuyaclaw_ctrl, pc_ctrl.
 *
 * Three MCP tools share one implementation: when ACP to the OpenClaw gateway
 * is connected, `message` is injected via `acp_client_inject()`. The tool
 * acknowledges send success; execution results are delivered asynchronously
 * (see acp_client.c / message_bus).
 *
 * @version 2.1
 * @date 2026-04-20
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */

#ifndef __TOOL_OPENCLAW_CTRL_H__
#define __TOOL_OPENCLAW_CTRL_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */

/**
 * @brief Register gateway PC control MCP tools.
 *
 * Registers:
 *   - "openclaw_ctrl"  – OpenClaw-branded task notification
 *   - "tuyaclaw_ctrl"  – same logic, TuyaClaw naming
 *   - "pc_ctrl"        – same logic, generic PC control naming
 *
 * @return OPRT_OK on success, error code on failure.
 */
OPERATE_RET tool_openclaw_ctrl_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __TOOL_OPENCLAW_CTRL_H__ */
