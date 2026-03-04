/**
 * @file context_builder.h
 * @brief context_builder module is used to 
 * @version 0.1
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __CONTEXT_BUILDER_H__
#define __CONTEXT_BUILDER_H__

#include "tuya_cloud_types.h"
#include <stddef.h>

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

size_t context_build_system_prompt(char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* __CONTEXT_BUILDER_H__ */
