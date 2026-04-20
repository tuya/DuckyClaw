/**
 * @file feishu_bot.h
 * @brief Feishu (Lark) bot channel for IM component
 * @version 1.0
 * @date 2025-02-13
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __FEISHU_BOT_H__
#define __FEISHU_BOT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "im_platform.h"
#include "cJSON.h"

/**
 * @brief Initialize feishu bot (load credentials from config/NVS)
 * @return OPRT_OK on success
 */
OPERATE_RET feishu_bot_init(void);

/**
 * @brief Start feishu bot WebSocket service
 * @return OPRT_OK on success
 */
OPERATE_RET feishu_bot_start(void);

/**
 * @brief Unified Feishu message send interface.
 *
 * @param[in] chat_id       target chat_id (oc_xxx) or open_id (ou_xxx)
 * @param[in] text          message text body
 * @return OPRT_OK on success
 */
OPERATE_RET feishu_send_message(const char *chat_id, const char *text);

/**
 * @brief Set feishu app_id credential
 * @param[in] app_id application ID
 * @return OPRT_OK on success
 */
OPERATE_RET feishu_set_app_id(const char *app_id);

/**
 * @brief Set feishu app_secret credential
 * @param[in] app_secret application secret
 * @return OPRT_OK on success
 */
OPERATE_RET feishu_set_app_secret(const char *app_secret);

/**
 * @brief Set allowed sender list (CSV of open_id/user_id)
 * @param[in] allow_from_csv comma-separated list of allowed sender IDs
 * @return OPRT_OK on success
 */
OPERATE_RET feishu_set_allow_from(const char *allow_from_csv);

#ifdef __cplusplus
}
#endif

#endif /* __FEISHU_BOT_H__ */
