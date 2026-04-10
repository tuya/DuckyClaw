/**
 * @file app_im.c
 * @brief app_im module is used to 
 * @version 0.1
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "app_im.h"

#include "tal_api.h"

#include "im_api.h"
#include "ai_agent.h"
#include "ai_chat_main.h"
#include "tal_system.h"
#include "tuya_app_config.h"
#include "channels/feishu_bot.h"
#include "ws_server.h"
#include <stdatomic.h>

/***********************************************************
************************macro define************************
***********************************************************/
#define INBOUND_POLL_MS  100

/***********************************************************
***********************typedef define***********************
***********************************************************/


/***********************************************************
********************function declaration********************
***********************************************************/


/***********************************************************
***********************variable define**********************
***********************************************************/
static THREAD_HANDLE s_outbound_thd = NULL;
// static THREAD_HANDLE s_inbound_thd  = NULL;

static char *s_channel = NULL;
static char s_chat_id[96] = {0};

/***********************************************************
***********************function define**********************
***********************************************************/

static const char *__app_im_map_channel(const char *channel)
{
    if (!channel || channel[0] == '\0') {
        return NULL;
    }
    if (strcmp(channel, IM_CHAN_TELEGRAM) == 0) {
        return IM_CHAN_TELEGRAM;
    } else if (strcmp(channel, IM_CHAN_DISCORD) == 0) {
        return IM_CHAN_DISCORD;
    } else if (strcmp(channel, IM_CHAN_FEISHU) == 0) {
        return IM_CHAN_FEISHU;
    } else if (strcmp(channel, IM_CHAN_WEIXIN) == 0) {
        return IM_CHAN_WEIXIN;
    } else if (strcmp(channel, IM_CHAN_WS) == 0) {
        return IM_CHAN_WS;
    } else {
        return s_channel;
    }
}

static BOOL_T __app_im_ws_token_valid(void)
{
#ifdef CLAW_WS_AUTH_TOKEN
    return (CLAW_WS_AUTH_TOKEN[0] != '\0') ? TRUE : FALSE;
#else
    return FALSE;
#endif
}

void app_im_set_target(const char *channel, const char *chat_id)
{
    const char *mapped_channel = __app_im_map_channel(channel);

    if (mapped_channel) {
        s_channel = (char *)mapped_channel;
    }

    if (!chat_id || chat_id[0] == '\0') {
        return;
    }

    strncpy(s_chat_id, chat_id, sizeof(s_chat_id) - 1);
    s_chat_id[sizeof(s_chat_id) - 1] = '\0';

    if (!mapped_channel || strcmp(mapped_channel, IM_CHAN_WS) != 0) {
        (void)im_kv_set_string(IM_NVS_BOT, "chat_id", chat_id);
    }
}

const char *app_im_get_channel(void)
{
    return s_channel;
}

const char *app_im_get_chat_id(void)
{
    return (s_chat_id[0] != '\0') ? s_chat_id : NULL;
}

static void outbound_dispatch_task(void *arg)
{
    (void)arg;
    PR_INFO("outbound dispatcher started");
    while (1) {
        im_msg_t msg = {0};
        if (message_bus_pop_outbound(&msg, 0xffffffff) != OPRT_OK) continue;
        if (!msg.content) continue;

        if (strcmp(msg.channel, IM_CHAN_TELEGRAM) == 0) {
            (void)telegram_send_message(msg.chat_id, msg.content ? msg.content : "");
        } else if (strcmp(msg.channel, IM_CHAN_DISCORD) == 0) {
            (void)discord_send_message(msg.chat_id, msg.content ? msg.content : "");
        } else if (strcmp(msg.channel, IM_CHAN_FEISHU) == 0) {
            (void)feishu_send_message(msg.chat_id,
                                      msg.content ? msg.content : "",
                                      msg.mentions_json);
        } else if (strcmp(msg.channel, IM_CHAN_WEIXIN) == 0) {
            (void)weixin_send_message(msg.chat_id, msg.content ? msg.content : "");
        } else if (strcmp(msg.channel, IM_CHAN_WS) == 0) {
            if (!__app_im_ws_token_valid()) {
                PR_WARN("ws outbound dropped: CLAW_WS_AUTH_TOKEN is empty");
            } else if (msg.chat_id[0] == '\0') {
                PR_WARN("ws outbound dropped: empty chat_id");
            } else {
                OPERATE_RET ws_rt = ws_server_send(msg.chat_id, msg.content ? msg.content : "");
                if (ws_rt != OPRT_OK) {
                    PR_WARN("ws_server_send failed chat_id=%s rt=%d", msg.chat_id, ws_rt);
                }
            }
        } else if (strcmp(msg.channel, "system") == 0) {
            PR_INFO("system msg: %s", msg.content ? msg.content : "");
        }
        im_free(msg.content);
        im_free(msg.mentions_json);
    }
}

static OPERATE_RET start_outbound_dispatcher(void)
{
    if (s_outbound_thd) return OPRT_OK;
    THREAD_CFG_T cfg = {0};
    cfg.stackDepth = IM_OUTBOUND_STACK;
    cfg.priority   = THREAD_PRIO_1;
    cfg.thrdname   = "outbound_loop";
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    cfg.psram_mode = 1;
#endif
    OPERATE_RET rt = tal_thread_create_and_start(&s_outbound_thd, NULL, NULL, outbound_dispatch_task, NULL, &cfg);
    return rt;
}

static OPERATE_RET app_im_init_evt_cb(void *data)
{
    OPERATE_RET rt = OPRT_OK;

    PR_INFO("app im network connected, init im...");

    /* Restore last known chat_id from KV so cron/system messages work after reboot */
    if (s_chat_id[0] == '\0') {
        char saved_chat_id[96] = {0};
        if (im_kv_get_string(IM_NVS_BOT, "chat_id", saved_chat_id, sizeof(saved_chat_id)) == OPRT_OK
                && saved_chat_id[0] != '\0') {
            strncpy(s_chat_id, saved_chat_id, sizeof(s_chat_id) - 1);
            PR_INFO("app im restored chat_id=%s from KV", s_chat_id);
        }
    }

    char        mode_kv[16] = {0};
    const char *mode        = IM_SECRET_CHANNEL_MODE;
    if (im_kv_get_string(IM_NVS_BOT, IM_NVS_KEY_CHANNEL_MODE, mode_kv, sizeof(mode_kv)) == OPRT_OK &&
        mode_kv[0] != '\0') {
        mode = mode_kv;
    } else {
        PR_WARN("im channel_mode not set, fallback to %s", IM_SECRET_CHANNEL_MODE);
    }
    PR_INFO("im channel_mode=%s", mode);

    rt = message_bus_init();
    if (rt != OPRT_OK) {
        PR_ERR("message_bus_init failed rt:%d", rt);
        return rt;
    }

    //http_proxy_init();

    if (strcmp(mode, IM_CHAN_TELEGRAM) == 0) {
        rt = telegram_bot_init();
        if (rt == OPRT_OK) {
            rt = telegram_bot_start();
        }
        s_channel = IM_CHAN_TELEGRAM;
    } else if (strcmp(mode, IM_CHAN_DISCORD) == 0) {
        rt = discord_bot_init();
        if (rt == OPRT_OK) {
            rt = discord_bot_start();
        }
        s_channel = IM_CHAN_DISCORD;
    } else if (strcmp(mode, IM_CHAN_FEISHU) == 0) {
        rt = feishu_bot_init();
        if (rt == OPRT_OK) {
            rt = feishu_bot_start();
        }
        s_channel = IM_CHAN_FEISHU;
    } else if (strcmp(mode, IM_CHAN_WEIXIN) == 0) {
        rt = weixin_bot_init();
        if (rt == OPRT_OK) {
            rt = weixin_bot_start();
        }
        s_channel = IM_CHAN_WEIXIN;
    } else {
        PR_WARN("unknown channel_mode '%s', fallback to %s", mode, IM_SECRET_CHANNEL_MODE);
        rt = telegram_bot_init();
        if (rt == OPRT_OK) {
            rt = telegram_bot_start();
        }
        s_channel = IM_CHAN_TELEGRAM;
    }

    if (rt != OPRT_OK) {
        PR_ERR("im bot start failed rt:%d (mode=%s)", rt, mode);
        /* keep running loops so outbound/system messages still work */
    }

    start_outbound_dispatcher();

    return OPRT_OK;
}

OPERATE_RET app_im_init(void)
{
    PR_INFO("app im wait network...");
    return tal_event_subscribe(EVENT_MQTT_CONNECTED, "app_im_init", app_im_init_evt_cb, SUBSCRIBE_TYPE_NORMAL);
}

static OPERATE_RET app_im_bot_send_message_to(const char *channel,
                                       const char *chat_id,
                                       const char *message,
                                       const char *mentions_json)
{
    const char *target_channel = __app_im_map_channel(channel);
    const char *target_chat_id = chat_id;

    if (!message) {
        return OPRT_INVALID_PARM;
    }

    if (!target_channel) {
        target_channel = s_channel;
    }

    if (!target_chat_id || target_chat_id[0] == '\0') {
        target_chat_id = s_chat_id;
    }

    if (!target_channel) {
        PR_ERR("app_im not initialized, channel is NULL");
        return OPRT_RESOURCE_NOT_READY;
    }

    if (!target_chat_id || target_chat_id[0] == '\0') {
        PR_WARN("app_im chat_id not set, dropping message");
        return OPRT_INVALID_PARM;
    }

    PR_DEBUG("app im bot send message: %s", message);

    im_msg_t out = {0};
    strncpy(out.channel, target_channel, sizeof(out.channel) - 1);
    out.channel[sizeof(out.channel) - 1] = '\0';
    strncpy(out.chat_id, target_chat_id, sizeof(out.chat_id) - 1);
    out.chat_id[sizeof(out.chat_id) - 1] = '\0';

    PR_DEBUG("app im bot send message: channel=%s, chat_id=%s", out.channel, out.chat_id);

    out.content = im_malloc(strlen(message) + 1);
    if (!out.content) {
        return OPRT_MALLOC_FAILED;
    }
    memset(out.content, 0, strlen(message) + 1);
    strncpy(out.content, message, strlen(message) + 1);

    if (mentions_json && mentions_json[0] != '\0') {
        out.mentions_json = im_strdup(mentions_json);
        if (!out.mentions_json) {
            im_free(out.content);
            return OPRT_MALLOC_FAILED;
        }
    }

    OPERATE_RET rt = message_bus_push_outbound(&out);
    if (rt != OPRT_OK) {
        im_free(out.content);
        im_free(out.mentions_json);
    }
    return rt;
}

OPERATE_RET app_im_bot_send_message_with_mentions(const char *message, const char *mentions_json)
{
    return app_im_bot_send_message_to(NULL, NULL, message, mentions_json);
}

OPERATE_RET app_im_bot_send_message(const char *message)
{
    return app_im_bot_send_message_to(NULL, NULL, message, NULL);
}
