/**
 * @file weixin_bot.h
 * @brief Weixin (WeChat) iLink Bot channel for IM component.
 *
 * Login flow  : One-time QR scan via browser (URL logged to UART).
 * Inbound     : Long-poll POST /ilink/bot/getupdates  → message_bus inbound.
 * Outbound    : POST /ilink/bot/sendmessage           ← message_bus outbound.
 *
 * @version 1.0
 * @date 2026-03-31
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __WEIXIN_BOT_H__
#define __WEIXIN_BOT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "im_platform.h"

/* ---------------------------------------------------------------------------
 * Weixin (WeChat) iLink Bot configuration
 * --------------------------------------------------------------------------- */

#ifndef IM_WX_API_HOST
#define IM_WX_API_HOST             "ilinkai.weixin.qq.com"
#endif
#define IM_WX_POLL_TIMEOUT_S       35
#define IM_WX_QR_POLL_TIMEOUT_S    35
#define IM_WX_MAX_MSG_LEN          4096
#define IM_WX_POLL_STACK           (14 * 1024)
#define IM_WX_QR_STACK             (10 * 1024)
#define IM_WX_FAIL_BASE_MS         2000
#define IM_WX_FAIL_MAX_MS          60000
#define IM_WX_SESSION_PAUSE_MS     (60 * 60 * 1000)
#define IM_WX_LOGIN_TIMEOUT_MS     (8 * 60 * 1000)
#define IM_WX_QR_MAX_REFRESH       3
#define IM_WX_BOT_TYPE             "3"
#define IM_WX_CHANNEL_VERSION      "1.0.3"

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */

/**
 * @brief Initialize Weixin bot (load credentials from KV storage).
 * @return OPRT_OK on success
 */
OPERATE_RET weixin_bot_init(void);

/**
 * @brief Start Weixin bot.
 *
 * If a bot_token is already stored in KV, immediately starts the long-poll
 * task.  Otherwise starts a QR-login task that prints the scan URL to the
 * log; once the user completes the scan the task saves credentials and
 * automatically starts polling.
 *
 * @return OPRT_OK on success
 */
OPERATE_RET weixin_bot_start(void);

/**
 * @brief Send a text message to a Weixin user.
 *
 * @param[in] user_id  Target iLink user ID (e.g. "abc123@im.wechat")
 * @param[in] text     UTF-8 text content
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET weixin_send_message(const char *user_id, const char *text);

/**
 * @brief Persist a new bot_token and reset poll state.
 *
 * Useful for CLI-based re-login without a QR scan on the device.
 *
 * @param[in] token  New bot_token string
 * @return OPRT_OK on success
 */
OPERATE_RET weixin_set_token(const char *token);

/**
 * @brief Persist the authorised sender user_id (allow_from).
 *
 * Messages from any user_id NOT in the allow list are silently dropped.
 * Typically set automatically on QR login from ilink_user_id.
 *
 * @param[in] user_id  Weixin iLink user ID to authorise
 * @return OPRT_OK on success
 */
OPERATE_RET weixin_set_allow_from(const char *user_id);

#ifdef __cplusplus
}
#endif
#endif /* __WEIXIN_BOT_H__ */
