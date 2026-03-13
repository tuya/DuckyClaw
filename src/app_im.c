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

void app_im_set_chat_id(const char *chat_id)
{
    if (!chat_id || chat_id[0] == '\0') {
        return;
    }
    strncpy(s_chat_id, chat_id, sizeof(s_chat_id) - 1);
    /* Persist so cron/system messages can be delivered after reboot */
    (void)im_kv_set_string(IM_NVS_BOT, "chat_id", chat_id);
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
            (void)feishu_send_message(msg.chat_id, msg.content ? msg.content : "");
        } else if (strcmp(msg.channel, "system") == 0) {
            PR_INFO("system msg: %s", msg.content ? msg.content : "");
        }
        im_free(msg.content);
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
    return tal_thread_create_and_start(&s_outbound_thd, NULL, NULL, outbound_dispatch_task, NULL, &cfg);
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

OPERATE_RET app_im_bot_send_message(const char *message)
{
    if (!message) {
        return OPRT_INVALID_PARM;
    }

    if (!s_channel) {
        PR_ERR("app_im not initialized, channel is NULL");
        return OPRT_RESOURCE_NOT_READY;
    }

    if (s_chat_id[0] == '\0') {
        PR_WARN("app_im chat_id not set, dropping message");
        return OPRT_INVALID_PARM;
    }

    PR_DEBUG("app im bot send message: %s", message);

    im_msg_t out = {0};
    strncpy(out.channel, s_channel, sizeof(out.channel) - 1);
    strncpy(out.chat_id, s_chat_id, sizeof(out.chat_id) - 1);

    PR_DEBUG("app im bot send message: channel=%s, chat_id=%s", out.channel, out.chat_id);

    out.content = im_malloc(strlen(message) + 1);
    if (!out.content) {
        return OPRT_MALLOC_FAILED;
    }
    memset(out.content, 0, strlen(message) + 1);
    strncpy(out.content, message, strlen(message) + 1);

    message_bus_push_outbound(&out);

    return OPRT_OK;
}
