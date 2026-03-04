#ifndef __IM_SERIAL_CLI_H__
#define __IM_SERIAL_CLI_H__

#include "im_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register IM-related CLI commands.
 *
 * Commands include:
 * - im_config_show
 * - im_config_reset
 * - im_set_channel_mode
 * - im_set_tg_token / im_set_dc_token / im_set_dc_channel
 * - im_set_fs_appid / im_set_fs_appsecret / im_set_fs_allow
 * - im_set_proxy / im_clear_proxy
 */
OPERATE_RET serial_cli_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __IM_SERIAL_CLI_H__ */
