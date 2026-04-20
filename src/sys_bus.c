/**
 * @file sys_bus.c
 * @brief System message bus — pure message queue + outbound routing.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "sys_bus.h"
#include "app_base_config.h"
#include "tal_api.h"
#include <string.h>

/* ---- queue config ---- */

#define SYS_BUS_QUEUE_LEN     10
#define SYS_BUS_MAX_SENDERS   12

/* ---- outbound route table ---- */

typedef struct {
    char                name[16];
    sys_channel_send_fn send;
} sys_sender_entry_t;

static sys_sender_entry_t s_senders[SYS_BUS_MAX_SENDERS];
static int                s_sender_count = 0;

/* ---- queues ---- */
static QUEUE_HANDLE  s_inbound_queue  = NULL;
static QUEUE_HANDLE  s_outbound_queue = NULL;

/* ---- outbound dispatch thread ---- */
static THREAD_HANDLE s_outbound_thd = NULL;

/* ---- outbound dispatch task ---- */
static OPERATE_RET sys_bus_start_dispatch(void *data);

static void __outbound_dispatch_task(void *arg)
{
    (void)arg;
    PR_INFO("sys_bus: outbound dispatcher started");

    while (1) {
        sys_msg_t msg = {0};
        if (tal_queue_fetch(s_outbound_queue, &msg, QUEUE_WAIT_FOREVER) != OPRT_OK)
            continue;
        if (!msg.content) continue;

        /* look up matching channel in route table */
        bool dispatched = false;
        for (int i = 0; i < s_sender_count; i++) {
            if (strcmp(msg.channel, s_senders[i].name) != 0) continue;

            if (s_senders[i].send) {
                s_senders[i].send(msg.chat_id,
                                  msg.content);
            }
            dispatched = true;
            break;
        }

        if (!dispatched) {
            PR_WARN("sys_bus: no sender for channel '%s', dropping", msg.channel);
        }

        claw_free(msg.content);
    }
}

/* ---- public API ---- */

OPERATE_RET sys_bus_init(void)
{
    if (s_inbound_queue && s_outbound_queue) return OPRT_OK;

    OPERATE_RET rt = tal_queue_create_init(&s_inbound_queue,
                                           sizeof(sys_msg_t), SYS_BUS_QUEUE_LEN);
    if (rt != OPRT_OK) {
        PR_ERR("sys_bus: create inbound queue failed: %d", rt);
        return rt;
    }
    rt = tal_queue_create_init(&s_outbound_queue,
                               sizeof(sys_msg_t), SYS_BUS_QUEUE_LEN);
    if (rt != OPRT_OK) {
        PR_ERR("sys_bus: create outbound queue failed: %d", rt);
        tal_queue_free(s_inbound_queue);
        s_inbound_queue = NULL;
        return rt;
    }

    s_sender_count = 0;

    tal_event_subscribe(EVENT_MQTT_CONNECTED, "sys_bus_start_dispatch", sys_bus_start_dispatch, SUBSCRIBE_TYPE_NORMAL);
    PR_INFO("sys_bus initialized");
    return rt;
}

OPERATE_RET sys_bus_register_sender(const char *channel_name,
                                    sys_channel_send_fn send_fn)
{
    if (!channel_name || !send_fn) return OPRT_INVALID_PARM;
    if (s_sender_count >= SYS_BUS_MAX_SENDERS) {
        PR_ERR("sys_bus: sender table full");
        return OPRT_EXCEED_UPPER_LIMIT;
    }

    strncpy(s_senders[s_sender_count].name, channel_name,
            sizeof(s_senders[0].name) - 1);
    s_senders[s_sender_count].send = send_fn;
    s_sender_count++;

    PR_INFO("sys_bus: registered sender '%s'", channel_name);
    return OPRT_OK;
}

static OPERATE_RET sys_bus_start_dispatch(void *data)
{
    (void)data;
    if (s_outbound_thd) return OPRT_OK;

    THREAD_CFG_T cfg = {0};
    cfg.stackDepth = SYS_BUS_OUTBOUND_STACK_SIZE;
    cfg.priority   = THREAD_PRIO_1;
    cfg.thrdname   = "sys_bus_out";
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    cfg.psram_mode = 1;
#endif
    OPERATE_RET rt = tal_thread_create_and_start(&s_outbound_thd, NULL, NULL,
                                       __outbound_dispatch_task, NULL, &cfg);
    if (rt != OPRT_OK) {
        PR_ERR("sys_bus: start outbound dispatch thread failed: %d", rt);
        return rt;
    }
    return rt;
}

OPERATE_RET sys_bus_push_inbound(const sys_msg_t *msg)
{
    if (!s_inbound_queue || !msg) return OPRT_INVALID_PARM;
    return tal_queue_post(s_inbound_queue, (void *)msg, 1000);
}

OPERATE_RET sys_bus_pop_inbound(sys_msg_t *msg, uint32_t timeout_ms)
{
    if (!s_inbound_queue || !msg) return OPRT_INVALID_PARM;
    return tal_queue_fetch(s_inbound_queue, msg,
                           timeout_ms == UINT32_MAX ? QUEUE_WAIT_FOREVER
                                                    : timeout_ms);
}

OPERATE_RET sys_bus_push_outbound(const sys_msg_t *msg)
{
    if (!s_outbound_queue || !msg) return OPRT_INVALID_PARM;
    return tal_queue_post(s_outbound_queue, (void *)msg, 1000);
}
