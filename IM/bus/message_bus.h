#ifndef __MESSAGE_BUS_H__
#define __MESSAGE_BUS_H__

#include "im_platform.h"
#include "sys_bus.h"

#define IM_CHAN_OFF       "OFF"
#define IM_CHAN_TELEGRAM  SYS_CHAN_TELEGRAM
#define IM_CHAN_DISCORD   SYS_CHAN_DISCORD
#define IM_CHAN_FEISHU    SYS_CHAN_FEISHU
#define IM_CHAN_WEIXIN    SYS_CHAN_WEIXIN
#define IM_CHAN_WS        SYS_CHAN_WS
#define IM_CHAN_ACP       SYS_CHAN_ACP

/**
 * @brief Inbound/outbound message on the IM bus.
 */
typedef struct {
    char  channel[16];
    char  chat_id[96];
    char *content;
    char *mentions_json;
} im_msg_t;

OPERATE_RET message_bus_init(void);
OPERATE_RET message_bus_push_inbound(const im_msg_t *msg);
OPERATE_RET message_bus_pop_inbound(im_msg_t *msg, uint32_t timeout_ms);
OPERATE_RET message_bus_push_outbound(const im_msg_t *msg);
OPERATE_RET message_bus_pop_outbound(im_msg_t *msg, uint32_t timeout_ms);

#endif /* __MESSAGE_BUS_H__ */
