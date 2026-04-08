/**
 * @file app_config_kv.h
 * @brief KV-backed application configuration helpers.
 *
 * Provides runtime-overridable configuration for compile-time macros defined in
 * tuya_app_config.h. Each getter checks KV storage first, then falls back to
 * the compile-time default.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __APP_CONFIG_KV_H__
#define __APP_CONFIG_KV_H__

#include "tal_kv.h"
#include "tuya_app_config.h"
#include "tuya_cloud_types.h"

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- KV key definitions ---- */

#define APP_KV_PRODUCT_ID   "app.product_id"
#define APP_KV_UUID         "app.uuid"
#define APP_KV_AUTHKEY      "app.authkey"
#define APP_KV_WS_TOKEN     "ws_auth_token"   /* reuse existing ws_server key */
#define APP_KV_GW_HOST      "app.gw_host"
#define APP_KV_GW_PORT      "app.gw_port"
#define APP_KV_GW_TOKEN     "app.gw_token"
#define APP_KV_DEVICE_ID    "app.device_id"

/* ---- Generic string getter: KV first, then compile-time default ---- */

static inline OPERATE_RET app_kv_get_string(const char *kv_key, const char *build_default,
                                            char *out, size_t out_size)
{
    if (!kv_key || !out || out_size == 0) return OPRT_INVALID_PARM;

    uint8_t *buf = NULL;
    size_t   len = 0;
    OPERATE_RET rt = tal_kv_get(kv_key, &buf, &len);
    if (rt == OPRT_OK && buf && len > 0 && ((char *)buf)[0] != '\0') {
        size_t copy = (len < out_size - 1) ? len : (out_size - 1);
        memcpy(out, buf, copy);
        out[copy] = '\0';
        tal_kv_free(buf);
        return OPRT_OK;
    }
    if (buf) tal_kv_free(buf);

    if (build_default && build_default[0] != '\0') {
        snprintf(out, out_size, "%s", build_default);
        return OPRT_OK;
    }

    out[0] = '\0';
    return OPRT_NOT_FOUND;
}

static inline OPERATE_RET app_kv_set_string(const char *kv_key, const char *value)
{
    if (!kv_key || !value) return OPRT_INVALID_PARM;
    return tal_kv_set(kv_key, (const uint8_t *)value, strlen(value) + 1);
}

static inline OPERATE_RET app_kv_del(const char *kv_key)
{
    if (!kv_key) return OPRT_INVALID_PARM;
    return tal_kv_del(kv_key);
}

/* ---- Typed getters for each config item ---- */

static inline void app_cfg_get_product_id(char *out, size_t out_size)
{
    app_kv_get_string(APP_KV_PRODUCT_ID, TUYA_PRODUCT_ID, out, out_size);
}

static inline void app_cfg_get_uuid(char *out, size_t out_size)
{
    app_kv_get_string(APP_KV_UUID, TUYA_OPENSDK_UUID, out, out_size);
}

static inline void app_cfg_get_authkey(char *out, size_t out_size)
{
    app_kv_get_string(APP_KV_AUTHKEY, TUYA_OPENSDK_AUTHKEY, out, out_size);
}

static inline void app_cfg_get_ws_token(char *out, size_t out_size)
{
    app_kv_get_string(APP_KV_WS_TOKEN, CLAW_WS_AUTH_TOKEN, out, out_size);
}

static inline void app_cfg_get_gw_host(char *out, size_t out_size)
{
    app_kv_get_string(APP_KV_GW_HOST, OPENCLAW_GATEWAY_HOST, out, out_size);
}

static inline void app_cfg_get_gw_port(char *out, size_t out_size)
{
    char port_str[8] = {0};
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)OPENCLAW_GATEWAY_PORT);
    app_kv_get_string(APP_KV_GW_PORT, port_str, out, out_size);
}

static inline unsigned app_cfg_get_gw_port_num(void)
{
    char buf[8] = {0};
    app_cfg_get_gw_port(buf, sizeof(buf));
    unsigned port = (unsigned)strtoul(buf, NULL, 10);
    return (port > 0 && port <= 65535) ? port : (unsigned)OPENCLAW_GATEWAY_PORT;
}

static inline void app_cfg_get_gw_token(char *out, size_t out_size)
{
    app_kv_get_string(APP_KV_GW_TOKEN, OPENCLAW_GATEWAY_TOKEN, out, out_size);
}

static inline void app_cfg_get_device_id(char *out, size_t out_size)
{
    app_kv_get_string(APP_KV_DEVICE_ID, DUCKYCLAW_DEVICE_ID, out, out_size);
}

#ifdef __cplusplus
}
#endif

#endif /* __APP_CONFIG_KV_H__ */
