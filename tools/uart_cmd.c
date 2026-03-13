/**
 * @file uart_cmd.c
 * @brief UART command read/write MCP tools for DuckyClaw
 * @version 0.1
 * @date 2025-03-25
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 * Implements uart_write, uart_read and uart_exec MCP tools using UART2 (PIN40/41)
 * for sending commands to and reading responses from a connected computer.
 * The computer runs duckyclaw.py which listens on serial, executes received
 * commands, and sends back the output.
 */

#include "uart_cmd.h"

#include "tal_api.h"
#include "tkl_pinmux.h"

#include <string.h>

/***********************************************************
************************macro define************************
***********************************************************/
#define UART_CMD_PORT       TUYA_UART_NUM_2
#define UART_CMD_BAUDRATE   115200
#define UART_CMD_RX_PIN     TUYA_IO_PIN_40
#define UART_CMD_TX_PIN     TUYA_IO_PIN_41
#define UART_CMD_BUF_SIZE   (4 * 1024)  /* 4KB read buffer */

/***********************************************************
***********************variable define**********************
***********************************************************/
static char sg_read_buffer[UART_CMD_BUF_SIZE];
static uint32_t sg_read_index = 0;
static bool sg_uart_inited = false;
static MUTEX_HANDLE sg_uart_mutex = NULL;

/***********************************************************
***********************function define**********************
***********************************************************/

/**
 * @brief Collect any pending data from UART into the internal buffer
 *
 * Reads all currently available data from the UART port (non-blocking) and
 * appends it to sg_read_buffer. In non-blocking mode tal_uart_read returns
 * immediately with <= 0 when no data is available, so this never hangs.
 */
static void __uart_collect_data(void)
{
    if (!sg_uart_inited) {
        return;
    }

    char tmp[256];
    int len;

    while (sg_read_index < UART_CMD_BUF_SIZE - 1) {
        size_t space = UART_CMD_BUF_SIZE - 1 - sg_read_index;
        size_t want  = sizeof(tmp) < space ? sizeof(tmp) : space;
        len = tal_uart_read(UART_CMD_PORT, (uint8_t *)tmp, want);
        if (len <= 0) {
            break;
        }
        memcpy(sg_read_buffer + sg_read_index, tmp, len);
        sg_read_index += len;
    }
    sg_read_buffer[sg_read_index] = '\0';
}

/**
 * @brief Poll UART for response data with a deadline.
 *
 * Calls __uart_collect_data() in a loop, sleeping UART_POLL_INTERVAL_MS between
 * iterations, until either data arrives or deadline_ms elapses.
 */
#define UART_POLL_INTERVAL_MS 50

static void __uart_wait_for_data(uint32_t deadline_ms)
{
    uint32_t elapsed = 0;
    while (sg_read_index == 0 && elapsed < deadline_ms) {
        tal_mutex_unlock(sg_uart_mutex);
        tal_system_sleep(UART_POLL_INTERVAL_MS);
        tal_mutex_lock(sg_uart_mutex);
        __uart_collect_data();
        elapsed += UART_POLL_INTERVAL_MS;
    }
}

/**
 * @brief Helper to extract a string property from MCP property list
 */
static const char *__get_str_prop(const MCP_PROPERTY_LIST_T *properties, const char *name)
{
    const MCP_PROPERTY_T *prop = ai_mcp_property_list_find(properties, name);
    if (prop && prop->type == MCP_PROPERTY_TYPE_STRING) {
        return prop->default_val.str_val;
    }
    return NULL;
}

/**
 * @brief MCP tool callback: uart_exec
 *
 * Sends a command to the connected computer via UART2, waits for execution,
 * then reads and returns the result. This is a convenience tool combining
 * uart_write + sleep + uart_read.
 *
 * Properties:
 * - command (string, required): The shell command to execute on the computer
 * - wait_ms (int, optional): Milliseconds to wait for execution (default 3000)
 */
static OPERATE_RET __tool_uart_exec(const MCP_PROPERTY_LIST_T *properties,
                                    MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    if (!sg_uart_inited) {
        ai_mcp_return_value_set_str(ret_val, "Error: UART not initialized");
        return OPRT_COM_ERROR;
    }

    const char *command = __get_str_prop(properties, "command");
    if (!command || command[0] == '\0') {
        ai_mcp_return_value_set_str(ret_val, "Error: 'command' is required and must not be empty");
        return OPRT_INVALID_PARM;
    }

    /* Get wait time, default 3000ms */
    int wait_ms = 3000;
    const MCP_PROPERTY_T *wait_prop = ai_mcp_property_list_find(properties, "wait_ms");
    if (wait_prop && wait_prop->type == MCP_PROPERTY_TYPE_INTEGER) {
        wait_ms = wait_prop->default_val.int_val;
        if (wait_ms < 500) wait_ms = 500;
        if (wait_ms > 30000) wait_ms = 30000;
    }

    tal_mutex_lock(sg_uart_mutex);

    /* Clear any old data in buffer before sending */
    sg_read_index = 0;
    memset(sg_read_buffer, 0, UART_CMD_BUF_SIZE);

    /* Drain any stale data already sitting in the UART hardware buffer.
     * Non-blocking reads return <= 0 immediately when the buffer is empty. */
    {
        char tmp[256];
        while (tal_uart_read(UART_CMD_PORT, (uint8_t *)tmp, sizeof(tmp)) > 0) {}
    }

    /* Send command */
    int cmd_len = strlen(command);
    int write_len = tal_uart_write(UART_CMD_PORT, (const uint8_t *)command, cmd_len);

    /* Append \r\n if needed */
    if (cmd_len > 0 && command[cmd_len - 1] != '\n' && command[cmd_len - 1] != '\r') {
        tal_uart_write(UART_CMD_PORT, (const uint8_t *)"\r\n", 2);
    }

    if (write_len < 0) {
        tal_mutex_unlock(sg_uart_mutex);
        ai_mcp_return_value_set_str(ret_val, "Error: UART write failed");
        return OPRT_COM_ERROR;
    }

    PR_DEBUG("uart_exec: sent '%s' (%d bytes), waiting up to %dms", command, write_len, wait_ms);

    /* Poll for response instead of a single blocking sleep, so this function
     * returns as soon as data arrives rather than always waiting the full
     * wait_ms, and so the calling thread is never permanently blocked. */
    __uart_wait_for_data((uint32_t)wait_ms);

    if (sg_read_index == 0) {
        tal_mutex_unlock(sg_uart_mutex);
        ai_mcp_return_value_set_str(ret_val, "(no response received from computer)");
        return OPRT_OK;
    }

    sg_read_buffer[sg_read_index] = '\0';
    ai_mcp_return_value_set_str(ret_val, sg_read_buffer);

    PR_DEBUG("uart_exec: response %u bytes", (unsigned)sg_read_index);

    /* Clear buffer */
    sg_read_index = 0;
    memset(sg_read_buffer, 0, UART_CMD_BUF_SIZE);

    tal_mutex_unlock(sg_uart_mutex);

    return OPRT_OK;
}

/**
 * @brief MCP tool callback: uart_write
 *
 * Sends a string command to the connected computer via UART2.
 * Automatically appends \r\n if the command does not end with a newline.
 *
 * Properties:
 * - command (string, required): The command string to send via UART
 */
static OPERATE_RET __tool_uart_write(const MCP_PROPERTY_LIST_T *properties,
                                     MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    if (!sg_uart_inited) {
        ai_mcp_return_value_set_str(ret_val, "Error: UART not initialized");
        return OPRT_COM_ERROR;
    }

    const char *command = __get_str_prop(properties, "command");
    if (!command || command[0] == '\0') {
        ai_mcp_return_value_set_str(ret_val, "Error: 'command' is required and must not be empty");
        return OPRT_INVALID_PARM;
    }

    tal_mutex_lock(sg_uart_mutex);

    int cmd_len = strlen(command);
    int write_len = tal_uart_write(UART_CMD_PORT, (const uint8_t *)command, cmd_len);

    /* Append \r\n if the command doesn't end with newline */
    if (cmd_len > 0 && command[cmd_len - 1] != '\n' && command[cmd_len - 1] != '\r') {
        tal_uart_write(UART_CMD_PORT, (const uint8_t *)"\r\n", 2);
    }

    tal_mutex_unlock(sg_uart_mutex);

    if (write_len < 0) {
        ai_mcp_return_value_set_str(ret_val, "Error: UART write failed");
        return OPRT_COM_ERROR;
    }

    char result[128];
    snprintf(result, sizeof(result), "OK: sent %d bytes via UART", write_len);
    ai_mcp_return_value_set_str(ret_val, result);

    PR_DEBUG("uart_write: sent %d bytes", write_len);
    return OPRT_OK;
}

/**
 * @brief MCP tool callback: uart_read
 *
 * Reads buffered data from UART2 and returns it as a string.
 * Clears the buffer after each read.
 *
 * Properties: (none required)
 */
static OPERATE_RET __tool_uart_read(const MCP_PROPERTY_LIST_T *properties,
                                    MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    if (!sg_uart_inited) {
        ai_mcp_return_value_set_str(ret_val, "Error: UART not initialized");
        return OPRT_COM_ERROR;
    }

    tal_mutex_lock(sg_uart_mutex);

    /* Collect any pending data from UART hardware buffer */
    __uart_collect_data();

    if (sg_read_index == 0) {
        tal_mutex_unlock(sg_uart_mutex);
        ai_mcp_return_value_set_str(ret_val, "(empty - no data received)");
        return OPRT_OK;
    }

    /* Return buffer content */
    sg_read_buffer[sg_read_index] = '\0';
    ai_mcp_return_value_set_str(ret_val, sg_read_buffer);

    PR_DEBUG("uart_read: %u bytes", (unsigned)sg_read_index);

    /* Clear buffer after read */
    sg_read_index = 0;
    memset(sg_read_buffer, 0, UART_CMD_BUF_SIZE);

    tal_mutex_unlock(sg_uart_mutex);

    return OPRT_OK;
}

/**
 * @brief Initialize UART2 for command communication
 *
 * Configures PIN40 as UART2 RX and PIN41 as UART2 TX,
 * then initializes UART2 at 115200 baud, 8N1.
 *
 * @return OPERATE_RET OPRT_OK on success, error code on failure
 */
OPERATE_RET uart_cmd_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    if (sg_uart_inited) {
        return OPRT_OK;
    }

    /* Create mutex for thread safety */
    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&sg_uart_mutex));

    /* Configure UART2 pinmux: PIN40 = RX, PIN41 = TX */
    tkl_io_pinmux_config(UART_CMD_RX_PIN, TUYA_UART2_RX);
    tkl_io_pinmux_config(UART_CMD_TX_PIN, TUYA_UART2_TX);

    /* Initialize UART2 */
    TAL_UART_CFG_T cfg = {0};
    cfg.base_cfg.baudrate = UART_CMD_BAUDRATE;
    cfg.base_cfg.databits = TUYA_UART_DATA_LEN_8BIT;
    cfg.base_cfg.stopbits = TUYA_UART_STOP_LEN_1BIT;
    cfg.base_cfg.parity   = TUYA_UART_PARITY_TYPE_NONE;
    cfg.rx_buffer_size    = UART_CMD_BUF_SIZE;
    cfg.open_mode         = 0;

    TUYA_CALL_ERR_GOTO(tal_uart_init(UART_CMD_PORT, &cfg), __EXIT);

    /* Clear read buffer */
    memset(sg_read_buffer, 0, UART_CMD_BUF_SIZE);
    sg_read_index = 0;
    sg_uart_inited = true;

    PR_NOTICE("UART cmd initialized: port=%d, baud=%d, pins RX=%d TX=%d, buf=%d",
              UART_CMD_PORT, UART_CMD_BAUDRATE,
              UART_CMD_RX_PIN, UART_CMD_TX_PIN, UART_CMD_BUF_SIZE);

    return OPRT_OK;

__EXIT:
    tal_mutex_release(sg_uart_mutex);
    sg_uart_mutex = NULL;
    PR_ERR("UART cmd init failed: %d", rt);
    return rt;
}

/**
 * @brief Deinitialize UART2
 *
 * @return OPERATE_RET OPRT_OK on success, error code on failure
 */
OPERATE_RET uart_cmd_deinit(void)
{
    if (!sg_uart_inited) {
        return OPRT_OK;
    }

    sg_uart_inited = false;
    tal_uart_deinit(UART_CMD_PORT);

    if (sg_uart_mutex) {
        tal_mutex_release(sg_uart_mutex);
        sg_uart_mutex = NULL;
    }

    sg_read_index = 0;
    memset(sg_read_buffer, 0, UART_CMD_BUF_SIZE);

    PR_NOTICE("UART cmd deinitialized");
    return OPRT_OK;
}

/**
 * @brief Register UART read/write MCP tools
 *
 * @return OPERATE_RET OPRT_OK on success
 */
OPERATE_RET uart_cmd_register(void)
{
    OPERATE_RET rt = OPRT_OK;

    /* Initialize UART hardware first */
    TUYA_CALL_ERR_RETURN(uart_cmd_init());

    /* uart_exec tool - combined write + wait + read */
    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "uart_exec",
        "Execute a shell command on the connected computer via UART serial port.\n"
        "This device is connected to a computer via UART2. The computer listens on the serial port,\n"
        "executes any received command in its shell, and sends the output back via serial.\n"
        "This tool sends the command, waits for execution, and returns the output.\n"
        "Use this when the user asks to run commands like ls, cat, ping, etc.\n"
        // "IMPORTANT CONSTRAINTS:\n"
        // "1. NON-INTERACTIVE ONLY: stdin is /dev/null. Commands that prompt for user input\n"
        // "   (passwords, confirmations, etc.) will immediately get EOF and fail.\n"
        // "   - git clone via HTTPS with auth -> use SSH URL (git@github.com:...) or a public repo\n"
        // "   - apt install -> always add -y flag\n"
        // "   - Any interactive prompt -> find the non-interactive equivalent flag\n"
        // "2. LONG-RUNNING COMMANDS: git clone, npm install, make, etc. take time.\n"
        // "   Set wait_ms to at least 30000 (30s) or more. Default 3000ms is too short.\n"
        // "3. SHELL SYNTAX: Full bash syntax is supported (pipes, redirects, &&, etc.).\n"
        // "   cd does not persist between calls; use 'cd /path && command' in one call.\n"
        "Parameters:\n"
        "- command (string): The shell command to execute (e.g. 'ls', 'cat /etc/hostname').\n"
        "- wait_ms (int, optional): Time to wait for execution in milliseconds (default 3000, range 500-30000).\n"
        "Response:\n"
        "- Returns the command output from the computer.",
        __tool_uart_exec,
        NULL,
        MCP_PROP_STR("command", "The shell command to execute on the connected computer"),
        &(MCP_PROPERTY_DEF_T){ .name = "wait_ms", .type = MCP_PROPERTY_TYPE_INTEGER,
            .description = "Milliseconds to wait for command execution (default 3000)",
            .has_default = TRUE, .default_val.int_val = 3000, .has_range = TRUE,
            .min_val = 500, .max_val = 30000 }
    ));

    /* uart_write tool */
    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "uart_write",
        "Send a raw string to the connected computer via UART serial port.\n"
        "Use this for sending data without waiting for a response.\n"
        "A \\r\\n is auto-appended if the string does not end with a newline.\n"
        "For running commands and getting output, prefer 'uart_exec' instead.\n"
        "Parameters:\n"
        "- command (string): The string to send.\n"
        "Response:\n"
        "- Returns the number of bytes sent.",
        __tool_uart_write,
        NULL,
        MCP_PROP_STR("command", "The string to send via UART")
    ));

    /* uart_read tool */
    TUYA_CALL_ERR_RETURN(AI_MCP_TOOL_ADD(
        "uart_read",
        "Read buffered response data from the connected computer via UART.\n"
        "Returns all data received since the last read, then clears the buffer.\n"
        "For running commands and getting output, prefer 'uart_exec' instead.\n"
        "Response:\n"
        "- Returns all buffered data as a string.",
        __tool_uart_read,
        NULL
    ));

    PR_DEBUG("UART cmd MCP tools registered successfully");
    return OPRT_OK;
}
