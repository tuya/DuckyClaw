/**
 * @file sys_bus.h
 * @brief System message bus — pure message queue, no channel lifecycle management.
 *
 * sys_bus is responsible for:
 *   1. Defining the unified message structure sys_msg_t
 *   2. Maintaining inbound / outbound message queues
 *   3. Registering outbound sender callbacks (routed by channel name)
 *
 * Each module manages its own initialization, start, and stop independently.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __SYS_BUS_H__
#define __SYS_BUS_H__

#include "tuya_cloud_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- channel name constants ---- */

#define SYS_CHAN_TELEGRAM  "telegram"
#define SYS_CHAN_DISCORD   "discord"
#define SYS_CHAN_FEISHU    "feishu"
#define SYS_CHAN_WEIXIN    "weixin"
#define SYS_CHAN_WS        "ws"
#define SYS_CHAN_ACP       "acp"
#define SYS_CHAN_CRON      "cron"
#define SYS_CHAN_HEARTBEAT "heartbeat"
#define SYS_CHAN_SYSTEM    "system"

/* ---- message structure ---- */

typedef struct {
    char  channel[16];       /**< message source channel name */
    char  chat_id[96];       /**< conversation ID */
    char *content;           /**< message content (heap-allocated, ownership transferred) */
    char *mentions_json;     /**< @-mention JSON (heap-allocated, may be NULL) */
} sys_msg_t;

/* ---- outbound sender callback ---- */

/**
 * @brief Channel sender callback signature.
 * @param chat_id  target conversation ID
 * @param text     message text
 * @param extra    additional data (e.g. mentions_json), may be NULL
 * @return OPRT_OK on success
 */
typedef OPERATE_RET (*sys_channel_send_fn)(const char *chat_id,
                                           const char *text,
                                           const char *extra);

/* ---- bus API ---- */

/** Initialize the bus: create inbound/outbound queues */
OPERATE_RET sys_bus_init(void);

/**
 * @brief Register an outbound sender callback.
 * @param channel_name channel name (e.g. SYS_CHAN_TELEGRAM)
 * @param send_fn      sender callback function
 * @return OPRT_OK on success
 */
OPERATE_RET sys_bus_register_sender(const char *channel_name,
                                    sys_channel_send_fn send_fn);

/** Push an inbound message (called by channels upon receiving a message) */
OPERATE_RET sys_bus_push_inbound(const sys_msg_t *msg);

/** Pop an inbound message (called by agent_loop, blocks until available) */
OPERATE_RET sys_bus_pop_inbound(sys_msg_t *msg, uint32_t timeout_ms);

/** Push an outbound message (called when agent replies) */
OPERATE_RET sys_bus_push_outbound(const sys_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* __SYS_BUS_H__ */
