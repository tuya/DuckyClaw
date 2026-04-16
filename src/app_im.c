/**
 * @file app_im.c
 * @brief app_im module is used to 
 * @version 0.1
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "app_im.h"

#include "tal_api.h"
#include "app_base_config.h"

#include "im_api.h"
#include "sys_bus.h"
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
static THREAD_HANDLE s_bridge_thd = NULL;

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

/* ---- sys_bus sender callbacks (one per IM channel) ---- */

static OPERATE_RET __send_telegram(const char *chat_id, const char *text, const char *extra)
{
    (void)extra;
    return telegram_send_message(chat_id, text ? text : "");
}

static OPERATE_RET __send_discord(const char *chat_id, const char *text, const char *extra)
{
    (void)extra;
    return discord_send_message(chat_id, text ? text : "");
}

static OPERATE_RET __send_feishu(const char *chat_id, const char *text, const char *extra)
{
    return feishu_send_message(chat_id, text ? text : "", extra);
}

static OPERATE_RET __send_weixin(const char *chat_id, const char *text, const char *extra)
{
    (void)extra;
    return weixin_send_message(chat_id, text ? text : "");
}

static OPERATE_RET __send_ws(const char *chat_id, const char *text, const char *extra)
{
    (void)extra;
    if (!__app_im_ws_token_valid()) {
        PR_WARN("ws outbound dropped: CLAW_WS_AUTH_TOKEN is empty");
        return OPRT_COM_ERROR;
    }
    if (!chat_id || chat_id[0] == '\0') {
        PR_WARN("ws outbound dropped: empty chat_id");
        return OPRT_INVALID_PARM;
    }
    return ws_server_send(chat_id, text ? text : "");
}

/* ---- IM inbound → sys_bus bridge thread ---- */

static void __im_bridge_task(void *arg)
{
    (void)arg;
    PR_INFO("app_im: IM→sys_bus bridge started");

    while (1) {
        im_msg_t im = {0};
        if (message_bus_pop_inbound(&im, 0xffffffff) != OPRT_OK) continue;
        if (!im.content) continue;

        /* Transfer ownership: im_msg_t → sys_msg_t (content pointers move) */
        sys_msg_t io = {0};
        strncpy(io.channel, im.channel, sizeof(io.channel) - 1);
        strncpy(io.chat_id, im.chat_id, sizeof(io.chat_id) - 1);
        io.content       = im.content;        /* pointer ownership transfer */
        io.mentions_json  = im.mentions_json;  /* pointer ownership transfer */

        if (sys_bus_push_inbound(&io) != OPRT_OK) {
            PR_ERR("app_im: sys_bus_push_inbound failed");
            claw_free(io.content);
            claw_free(io.mentions_json);
        }
    }
}

static OPERATE_RET start_im_bridge(void)
{
    if (s_bridge_thd) return OPRT_OK;
    THREAD_CFG_T cfg = {0};
    cfg.stackDepth = 2 * 1024;
    cfg.priority   = THREAD_PRIO_1;
    cfg.thrdname   = "im_bridge";
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    cfg.psram_mode = 1;
#endif
    return tal_thread_create_and_start(&s_bridge_thd, NULL, NULL,
                                       __im_bridge_task, NULL, &cfg);
}

/* ---- Register all IM senders with sys_bus ---- */

static void __register_im_senders(void)
{
    sys_bus_register_sender(SYS_CHAN_TELEGRAM, __send_telegram);
    sys_bus_register_sender(SYS_CHAN_DISCORD,  __send_discord);
    sys_bus_register_sender(SYS_CHAN_FEISHU,   __send_feishu);
    sys_bus_register_sender(SYS_CHAN_WEIXIN,   __send_weixin);
    sys_bus_register_sender(SYS_CHAN_WS,       __send_ws);
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
        PR_WARN("Turned off IM channel '%s'", mode);
        s_channel = IM_CHAN_OFF;
    }

    if (rt != OPRT_OK) {
        PR_ERR("IM channel '%s' initialization failed rt:%d", mode, rt);
        return rt;
    }

    __register_im_senders();
    start_im_bridge();

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

    sys_msg_t out = {0};
    strncpy(out.channel, target_channel, sizeof(out.channel) - 1);
    out.channel[sizeof(out.channel) - 1] = '\0';
    strncpy(out.chat_id, target_chat_id, sizeof(out.chat_id) - 1);
    out.chat_id[sizeof(out.chat_id) - 1] = '\0';

    PR_DEBUG("app im bot send message: channel=%s, chat_id=%s", out.channel, out.chat_id);

    out.content = claw_malloc(strlen(message) + 1);
    if (!out.content) {
        return OPRT_MALLOC_FAILED;
    }
    memset(out.content, 0, strlen(message) + 1);
    strncpy(out.content, message, strlen(message) + 1);

    if (mentions_json && mentions_json[0] != '\0') {
        size_t mlen = strlen(mentions_json) + 1;
        out.mentions_json = claw_malloc(mlen);
        if (!out.mentions_json) {
            claw_free(out.content);
            return OPRT_MALLOC_FAILED;
        }
        memcpy(out.mentions_json, mentions_json, mlen);
    }

    OPERATE_RET rt = sys_bus_push_outbound(&out);
    if (rt != OPRT_OK) {
        claw_free(out.content);
        claw_free(out.mentions_json);
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
