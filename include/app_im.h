/**
 * @file app_im.h
 * @brief app_im module is used to 
 * @version 0.1
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __APP_IM_H__
#define __APP_IM_H__

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
/**
 * @brief Initialize app_im.
 *
 * @return OPRT_OK on success
 */
OPERATE_RET app_im_init(void);

/**
 * @brief Send a bot message.
 *
 * @param[in] message Message text.
 * @return OPRT_OK on success
 */
OPERATE_RET app_im_bot_send_message(const char *message);

/**
 * @brief Set the target channel and chat_id.
 *
 * @param[in] channel Channel name.
 * @param[in] chat_id Chat ID.
 */
void app_im_set_target(const char *channel, const char *chat_id);

/**
 * @brief Get the current channel.
 *
 * @return Channel name.
 */
const char *app_im_get_channel(void);

/**
 * @brief Get the current chat_id.
 *
 * @return Chat ID.
 */
const char *app_im_get_chat_id(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_IM_H__ */
