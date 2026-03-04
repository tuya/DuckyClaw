/**
 * @file tools_register.h
 * @brief MCP module initialization and deinitialization functions
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TOOLS_REGISTER_H__
#define __TOOLS_REGISTER_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET tool_registry_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __TOOLS_REGISTER_H__ */