/**
 * @file tools_register.c
 * @brief MCP tools registration for DuckyClaw
 * @version 0.1
 * @date 2025-03-25
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#include "tools_register.h"
#include "tool_files.h"
#include "tool_cron.h"
#include "cron_service.h"
#include "heartbeat.h"

#include "tal_api.h"

/***********************************************************
***********************function define**********************
***********************************************************/

static OPERATE_RET __ai_mcp_init(void *data)
{
    OPERATE_RET rt = OPRT_OK;

    /* Initialize filesystem (mount SD card if needed, create default files) */
    TUYA_CALL_ERR_RETURN(tool_files_fs_init());

    /* Initialize and start cron service */
    TUYA_CALL_ERR_LOG(cron_service_init());
    TUYA_CALL_ERR_LOG(cron_service_start());

    /* Initialize and start heartbeat service */
    TUYA_CALL_ERR_LOG(heartbeat_init());
    TUYA_CALL_ERR_LOG(heartbeat_start());

    /* Register file operation tools */
    TUYA_CALL_ERR_RETURN(tool_files_register());

    /* Register cron tools */
    TUYA_CALL_ERR_RETURN(tool_cron_register());

    PR_DEBUG("MCP Server initialized successfully with tools");
    return rt;
}

/**
 * @brief Initialize and register all MCP tools
 *
 * This function is called during MCP server initialization to register
 * all custom tools for the DuckyClaw project.
 *
 * @return OPERATE_RET OPRT_OK on success, error code on failure
 */
OPERATE_RET tool_registry_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    tal_event_subscribe(EVENT_MQTT_CONNECTED, "ai_mcp_init", __ai_mcp_init, SUBSCRIBE_TYPE_ONETIME);

    PR_DEBUG("DuckyClaw MCP tools registered successfully");

    return rt;
}
