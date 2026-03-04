/**
 * @file agent_loop.h
 * @brief agent_loop module is used to 
 * @version 0.1
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __AGENT_LOOP_H__
#define __AGENT_LOOP_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/


/***********************************************************
***********************typedef define***********************
***********************************************************/


/***********************************************************
********************function declaration********************
***********************************************************/

OPERATE_RET agent_loop_init(void);

OPERATE_RET build_current_context(const char *role, const char *content);

#ifdef __cplusplus
}
#endif

#endif /* __AGENT_LOOP_H__ */
