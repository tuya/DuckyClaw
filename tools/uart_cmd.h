/**
 * @file uart_cmd.h
 * @brief UART command read/write MCP tools for DuckyClaw
 * @version 0.1
 * @date 2025-03-25
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __UART_CMD_H__
#define __UART_CMD_H__

#include "tuya_cloud_types.h"
#include "ai_mcp_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize UART2 for command communication
 *
 * Configures pin 40/41 for UART2 RX/TX and initializes the UART port.
 *
 * @return OPERATE_RET OPRT_OK on success, error code on failure
 */
OPERATE_RET uart_cmd_init(void);

/**
 * @brief Deinitialize UART2
 *
 * @return OPERATE_RET OPRT_OK on success, error code on failure
 */
OPERATE_RET uart_cmd_deinit(void);

/**
 * @brief Register UART MCP tools (uart_exec, uart_write, uart_read)
 *
 * Registers uart_exec, uart_write and uart_read tools with the MCP server.
 * The UART is connected to a computer running duckyclaw.py which executes
 * received commands and returns the output.
 *
 * @return OPERATE_RET OPRT_OK on success, error code on failure
 */
OPERATE_RET uart_cmd_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __UART_CMD_H__ */
