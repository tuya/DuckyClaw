/**
 * @file cli_cmd.c
 * @brief Unified CLI implementation for DuckyClaw
 * @version 0.1
 * @date 2026-04-08
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 */

#include "app_base_config.h"
#include "bus/message_bus.h"
#include "channels/discord_bot.h"
#include "channels/feishu_bot.h"
#include "channels/telegram_bot.h"
#include "im_config.h"
#include "im_platform.h"
#include "proxy/http_proxy.h"
#include "tools/tool_files.h"
#include "tuya_authorize.h"

#include "netmgr.h"
#include "tal_api.h"
#include "tal_cli.h"
#include "tal_kv.h"
#include "tal_log.h"
#include "tal_sw_timer.h"
#include "tuya_iot.h"
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
#include "netconn_wifi.h"
#include "tal_wifi.h"
#endif

#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

extern void netmgr_cmd(int argc, char *argv[]);
extern void tal_kv_cmd(int argc, char *argv[]);
extern void tal_thread_dump_watermark(void);

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define CLI_LINE_SIZE             256
#define CLI_VALUE_SIZE            512
#define CLI_MASK_SIZE             64
#define CLI_DEFAULT_TEXT_LIMIT    4096
#define CLI_DEFAULT_HEX_LIMIT     512
#define CLI_UUID_LENGTH           20
#define CLI_AUTHKEY_LENGTH        32

#if defined(CLAW_FS_ROOT_PATH_EMPTY) && (CLAW_FS_ROOT_PATH_EMPTY == 1)
#define CLI_FS_DEFAULT_PATH       "/"
#else
#define CLI_FS_DEFAULT_PATH       CLAW_FS_ROOT_PATH
#endif

#if !(defined(ENABLE_FILE_SYSTEM) && (ENABLE_FILE_SYSTEM == 1))
#define CLI_FS_INFO_NEED_FREE     1
#else
#define CLI_FS_INFO_NEED_FREE     0
#endif

/* ---------------------------------------------------------------------------
 * Typedefs
 * --------------------------------------------------------------------------- */
typedef OPERATE_RET (*CLI_SETTER_CB)(const char *value);

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
static void cmd_help(int argc, char *argv[]);
static void cmd_sys_status(int argc, char *argv[]);
static void cmd_sys_heap(int argc, char *argv[]);
static void cmd_sys_thread(int argc, char *argv[]);
static void cmd_sys_version(int argc, char *argv[]);
static void cmd_sys_tick(int argc, char *argv[]);
static void cmd_sys_log_level(int argc, char *argv[]);
static void cmd_sys_reboot(int argc, char *argv[]);
static void cmd_sys_iot_start(int argc, char *argv[]);
static void cmd_sys_iot_stop(int argc, char *argv[]);
static void cmd_sys_iot_restart(int argc, char *argv[]);
static void cmd_sys_iot_reset(int argc, char *argv[]);
static void cmd_sys_netmgr(int argc, char *argv[]);
static void cmd_sys_exec(int argc, char *argv[]);
static void cmd_sys_switch(int argc, char *argv[]);
static void cmd_sys_uptime(int argc, char *argv[]);
static void cmd_sys_random(int argc, char *argv[]);
static void cmd_sys_timer_count(int argc, char *argv[]);
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
static void cmd_sys_wifi_info(int argc, char *argv[]);
static void cmd_sys_wifi_scan(int argc, char *argv[]);
#endif
static void cmd_fs_ls(int argc, char *argv[]);
static void cmd_fs_stat(int argc, char *argv[]);
static void cmd_fs_cat(int argc, char *argv[]);
static void cmd_fs_hexdump(int argc, char *argv[]);
static void cmd_fs_write(int argc, char *argv[]);
static void cmd_fs_append(int argc, char *argv[]);
static void cmd_fs_rm(int argc, char *argv[]);
static void cmd_fs_mkdir(int argc, char *argv[]);
static void cmd_fs_mv(int argc, char *argv[]);
static void cmd_kv_get(int argc, char *argv[]);
static void cmd_kv_set(int argc, char *argv[]);
static void cmd_kv_del(int argc, char *argv[]);
static void cmd_kv_list(int argc, char *argv[]);
static void cmd_cfg_show(int argc, char *argv[]);
static void cmd_cfg_reset(int argc, char *argv[]);
static void cmd_cfg_set_product_id(int argc, char *argv[]);
static void cmd_cfg_set_auth(int argc, char *argv[]);
static void cmd_cfg_set_ws_token(int argc, char *argv[]);
static void cmd_cfg_set_gw_host(int argc, char *argv[]);
static void cmd_cfg_set_gw_port(int argc, char *argv[]);
static void cmd_cfg_set_gw_token(int argc, char *argv[]);
static void cmd_cfg_set_device_id(int argc, char *argv[]);
static void cmd_cfg_set_channel_mode(int argc, char *argv[]);
static void cmd_cfg_set_tg_token(int argc, char *argv[]);
static void cmd_cfg_set_dc_token(int argc, char *argv[]);
static void cmd_cfg_set_dc_channel(int argc, char *argv[]);
static void cmd_cfg_set_fs_appid(int argc, char *argv[]);
static void cmd_cfg_set_fs_appsecret(int argc, char *argv[]);
static void cmd_cfg_set_fs_allow(int argc, char *argv[]);
static void cmd_cfg_set_proxy(int argc, char *argv[]);
static void cmd_cfg_clear_proxy(int argc, char *argv[]);

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */
/**
 * @brief Echo a formatted line to CLI.
 * @param[in] fmt printf-style format string
 * @param[in] ... format arguments
 * @return none
 */
static void cli_echof_(const char *fmt, ...)
{
    char    line[CLI_LINE_SIZE] = {0};
    va_list args;

    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    tal_cli_echo(line);
}

/**
 * @brief Join CLI arguments into one space-separated string.
 * @param[in] argc argument count
 * @param[in] argv argument array
 * @param[in] start first index to join
 * @param[out] out output buffer
 * @param[in] out_size output buffer size
 * @return true if at least one argument was joined, false otherwise
 */
static bool cli_join_args_(int argc, char *argv[], int start, char *out, size_t out_size)
{
    size_t offset = 0;

    if (out == NULL || out_size == 0) {
        return false;
    }

    out[0] = '\0';
    if (argc <= start) {
        return false;
    }

    for (int i = start; i < argc && offset + 1 < out_size; i++) {
        int written = snprintf(out + offset, out_size - offset, "%s%s", (i == start) ? "" : " ", argv[i]);
        if (written < 0) {
            return false;
        }
        if ((size_t)written >= out_size - offset) {
            offset = out_size - 1;
            break;
        }
        offset += (size_t)written;
    }

    return true;
}

/**
 * @brief Copy and mask a secret value for CLI display.
 * @param[in] src input string
 * @param[out] out masked output buffer
 * @param[in] out_size masked output buffer size
 * @return none
 */
static void cli_mask_copy_(const char *src, char *out, size_t out_size)
{
    size_t len;
    size_t head_len;
    size_t tail_len;

    if (out == NULL || out_size == 0) {
        return;
    }

    if (src == NULL || src[0] == '\0') {
        snprintf(out, out_size, "(empty)");
        return;
    }

    len = strlen(src);
    head_len = (len < 4) ? len : 4;
    tail_len = (len < 4) ? len : 4;

    snprintf(out, out_size, "%.*s******%.*s",
             (int)head_len, src,
             (int)tail_len, src + len - tail_len);
}

/**
 * @brief Convert a boolean state to CLI text.
 * @param[in] value boolean input
 * @return textual representation
 */
static const char *cli_bool_to_str_(bool value)
{
    return value ? "true" : "false";
}

/**
 * @brief Convert TAL log level to CLI text.
 * @param[in] level log level enum
 * @return textual representation
 */
static const char *cli_log_level_to_str_(TAL_LOG_LEVEL_E level)
{
    switch (level) {
    case TAL_LOG_LEVEL_ERR:
        return "err";
    case TAL_LOG_LEVEL_WARN:
        return "warn";
    case TAL_LOG_LEVEL_NOTICE:
        return "notice";
    case TAL_LOG_LEVEL_INFO:
        return "info";
    case TAL_LOG_LEVEL_DEBUG:
        return "debug";
    case TAL_LOG_LEVEL_TRACE:
        return "trace";
    default:
        return "unknown";
    }
}

/**
 * @brief Parse CLI text into TAL log level.
 * @param[in] text input text
 * @param[out] level parsed log level
 * @return true on success, false on invalid text
 */
static bool cli_parse_log_level_(const char *text, TAL_LOG_LEVEL_E *level)
{
    if (text == NULL || level == NULL) {
        return false;
    }

    if (strcmp(text, "err") == 0) {
        *level = TAL_LOG_LEVEL_ERR;
    } else if (strcmp(text, "warn") == 0) {
        *level = TAL_LOG_LEVEL_WARN;
    } else if (strcmp(text, "notice") == 0) {
        *level = TAL_LOG_LEVEL_NOTICE;
    } else if (strcmp(text, "info") == 0) {
        *level = TAL_LOG_LEVEL_INFO;
    } else if (strcmp(text, "debug") == 0) {
        *level = TAL_LOG_LEVEL_DEBUG;
    } else if (strcmp(text, "trace") == 0) {
        *level = TAL_LOG_LEVEL_TRACE;
    } else {
        return false;
    }

    return true;
}

/**
 * @brief Print one app configuration item with source information.
 * @param[in] label display label
 * @param[in] kv_key KV key
 * @param[in] build_value compile-time fallback
 * @param[in] mask whether to mask value
 * @return none
 */
static void cli_print_app_cfg_item_(const char *label, const char *kv_key, const char *build_value, bool mask)
{
    char        kv_value[CLI_VALUE_SIZE] = {0};
    char        masked[CLI_MASK_SIZE]    = {0};
    const char *source                   = "not set";
    const char *value                    = "(empty)";
    uint8_t    *buf                      = NULL;
    size_t      len                      = 0;

    if (tal_kv_get(kv_key, &buf, &len) == OPRT_OK && buf != NULL && len > 0 && ((char *)buf)[0] != '\0') {
        size_t copy_len = (len < sizeof(kv_value) - 1) ? len : (sizeof(kv_value) - 1);
        memcpy(kv_value, buf, copy_len);
        kv_value[copy_len] = '\0';
        value              = kv_value;
        source             = "kv";
    } else if (build_value != NULL && build_value[0] != '\0') {
        value  = build_value;
        source = "build";
    }

    if (buf != NULL) {
        tal_kv_free(buf);
    }

    if (mask && strcmp(value, "(empty)") != 0) {
        cli_mask_copy_(value, masked, sizeof(masked));
        cli_echof_("  %-18s %s [%s]", label, masked, source);
        return;
    }

    cli_echof_("  %-18s %s [%s]", label, value, source);
}

/**
 * @brief Print one config item using an explicit value and source.
 * @param[in] label display label
 * @param[in] value current value
 * @param[in] source value source text
 * @param[in] mask whether to mask value
 * @return none
 */
static void cli_print_cfg_value_item_(const char *label, const char *value, const char *source, bool mask)
{
    char        masked[CLI_MASK_SIZE] = {0};
    const char *display_value         = value;

    if (display_value == NULL || display_value[0] == '\0') {
        display_value = "(empty)";
    }

    if (source == NULL || source[0] == '\0') {
        source = "not set";
    }

    if (mask && strcmp(display_value, "(empty)") != 0) {
        cli_mask_copy_(display_value, masked, sizeof(masked));
        cli_echof_("  %-18s %s [%s]", label, masked, source);
        return;
    }

    cli_echof_("  %-18s %s [%s]", label, display_value, source);
}

/**
 * @brief Print one IM configuration item with source information.
 * @param[in] label display label
 * @param[in] ns IM KV namespace
 * @param[in] key IM KV key
 * @param[in] build_value compile-time fallback
 * @param[in] mask whether to mask value
 * @return none
 */
static void cli_print_im_cfg_item_(const char *label, const char *ns, const char *key, const char *build_value, bool mask)
{
    char        kv_value[CLI_VALUE_SIZE] = {0};
    char        masked[CLI_MASK_SIZE]    = {0};
    const char *source                   = "not set";
    const char *value                    = "(empty)";

    if (im_kv_get_string(ns, key, kv_value, sizeof(kv_value)) == OPRT_OK && kv_value[0] != '\0') {
        value  = kv_value;
        source = "kv";
    } else if (build_value != NULL && build_value[0] != '\0') {
        value  = build_value;
        source = "build";
    }

    if (mask && strcmp(value, "(empty)") != 0) {
        cli_mask_copy_(value, masked, sizeof(masked));
        cli_echof_("  %-18s %s [%s]", label, masked, source);
        return;
    }

    cli_echof_("  %-18s %s [%s]", label, value, source);
}

/**
 * @brief Clear all application config KV overrides.
 * @return none
 */
static void cli_clear_app_cfg_overrides_(void)
{
    static const char *const s_keys[] = {
        APP_KV_PRODUCT_ID,
        APP_KV_UUID,
        APP_KV_AUTHKEY,
        APP_KV_WS_TOKEN,
        APP_KV_GW_HOST,
        APP_KV_GW_PORT,
        APP_KV_GW_TOKEN,
        APP_KV_DEVICE_ID,
    };

    for (size_t i = 0; i < sizeof(s_keys) / sizeof(s_keys[0]); i++) {
        (void)app_kv_del(s_keys[i]);
    }
}

/**
 * @brief Clear all IM config KV overrides.
 * @return none
 */
static void cli_clear_im_cfg_overrides_(void)
{
    (void)im_kv_del(IM_NVS_BOT, IM_NVS_KEY_CHANNEL_MODE);

    (void)im_kv_del(IM_NVS_TG, IM_NVS_KEY_TG_TOKEN);

    (void)im_kv_del(IM_NVS_DC, IM_NVS_KEY_DC_TOKEN);
    (void)im_kv_del(IM_NVS_DC, IM_NVS_KEY_DC_CHANNEL_ID);
    (void)im_kv_del(IM_NVS_DC, IM_NVS_KEY_DC_LAST_MSG_ID);

    (void)im_kv_del(IM_NVS_FS, IM_NVS_KEY_FS_APP_ID);
    (void)im_kv_del(IM_NVS_FS, IM_NVS_KEY_FS_APP_SECRET);
    (void)im_kv_del(IM_NVS_FS, IM_NVS_KEY_FS_ALLOW_FROM);

    (void)http_proxy_clear();
}

/**
 * @brief Update UUID and authkey together through the authorize interface.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @param[in] usage usage text
 * @return none
 */
static void cli_set_authorize_pair_(int argc, char *argv[], const char *usage)
{
    const char *uuid    = NULL;
    const char *authkey = NULL;
    OPERATE_RET rt;

    if (argc < 3) {
        cli_echof_("Usage: %s", usage);
        return;
    }

    uuid    = argv[1];
    authkey = argv[2];

    if (strlen(uuid) != CLI_UUID_LENGTH) {
        cli_echof_("ERR: uuid length must be %u", (unsigned)CLI_UUID_LENGTH);
        return;
    }

    if (strlen(authkey) != CLI_AUTHKEY_LENGTH) {
        cli_echof_("ERR: authkey length must be %u", (unsigned)CLI_AUTHKEY_LENGTH);
        return;
    }

    tal_cli_echo("Applying authorization update, device will reboot on success.");
    rt = tuya_authorize_write(uuid, authkey);
    if (rt != OPRT_OK) {
        cli_echof_("ERR: cfg_set_auth rt=%d", rt);
    }
}

/**
 * @brief Set an app config string value.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @param[in] usage usage text
 * @param[in] kv_key app config KV key
 * @param[in] label logical field label
 * @return none
 */
static void cli_set_app_cfg_value_(int argc, char *argv[], const char *usage, const char *kv_key, const char *label)
{
    OPERATE_RET rt;

    if (argc < 2) {
        cli_echof_("Usage: %s", usage);
        return;
    }

    rt = app_kv_set_string(kv_key, argv[1]);
    cli_echof_("%s: %s rt=%d", (rt == OPRT_OK) ? "OK" : "ERR", label, rt);
}

/**
 * @brief Set an IM config value through a setter callback.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @param[in] usage usage text
 * @param[in] label logical field label
 * @param[in] setter setter callback
 * @return none
 */
static void cli_set_im_cfg_value_(int argc, char *argv[], const char *usage, const char *label, CLI_SETTER_CB setter)
{
    OPERATE_RET rt;

    if (argc < 2) {
        cli_echof_("Usage: %s", usage);
        return;
    }

    rt = setter(argv[1]);
    if (rt != OPRT_OK) {
        cli_echof_("ERR: %s rt=%d", label, rt);
        return;
    }

    cli_echof_("OK: %s saved to KV", label);
}

/**
 * @brief Format a MAC address for CLI display.
 * @param[in] mac MAC address structure
 * @param[out] out output text buffer
 * @param[in] out_size output buffer size
 * @return none
 */
static void cli_format_mac_(const NW_MAC_S *mac, char *out, size_t out_size)
{
    if (mac == NULL || out == NULL || out_size == 0) {
        return;
    }

    snprintf(out, out_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac->mac[0], mac->mac[1], mac->mac[2], mac->mac[3], mac->mac[4], mac->mac[5]);
}

/**
 * @brief Print current heap information.
 * @return none
 */
static void cli_print_heap_info_(void)
{
    cli_echof_("heap.free         %d", tal_system_get_free_heap_size());
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    cli_echof_("psram.free        %d", tal_psram_get_free_heap_size());
#endif
}

/**
 * @brief Print current WiFi connection details.
 * @return none
 */
static void cli_print_wifi_info_(void)
{
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
    netconn_wifi_info_t wifi_info    = {0};
    uint8_t             bssid[6]     = {0};
    int8_t              wifi_rssi    = 0;
    OPERATE_RET         rt;

    rt = netmgr_conn_get(NETCONN_WIFI, NETCONN_CMD_SSID_PSWD, &wifi_info);
    if (rt == OPRT_OK && wifi_info.ssid[0] != '\0') {
        cli_echof_("wifi.ssid        %s", wifi_info.ssid);
    } else if (rt == OPRT_OK) {
        cli_echof_("wifi.ssid        (empty)");
    } else {
        cli_echof_("wifi.ssid        unavailable (rt=%d)", rt);
    }

    if (tal_wifi_get_bssid(bssid) == OPRT_OK) {
        cli_echof_("wifi.bssid       %02X:%02X:%02X:%02X:%02X:%02X",
                     bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    }

    if (tal_wifi_station_get_conn_ap_rssi(&wifi_rssi) == OPRT_OK) {
        cli_echof_("wifi.rssi        %d dBm", wifi_rssi);
    }
#endif
}

/**
 * @brief Print current network information.
 * @return none
 */
static void cli_print_network_info_(void)
{
    netmgr_status_e status = NETMGR_LINK_DOWN;
    NW_IP_S         ip     = {0};
    NW_MAC_S        mac    = {0};
    char            mac_text[32] = {0};
    OPERATE_RET     rt;

    rt = netmgr_conn_get(NETCONN_AUTO, NETCONN_CMD_STATUS, &status);
    if (rt == OPRT_OK) {
        cli_echof_("network.status   %s", NETMGR_STATUS_TO_STR(status));
    } else {
        cli_echof_("network.status   unavailable (rt=%d)", rt);
    }

    rt = netmgr_conn_get(NETCONN_AUTO, NETCONN_CMD_IP, &ip);
    if (rt == OPRT_OK) {
#if defined(ENABLE_IPv6) && (ENABLE_IPv6 == 1)
        if (IS_NW_IPV6_ADDR(&ip)) {
            cli_echof_("network.ip       %s", ip.addr.ip6.ip);
        } else {
            cli_echof_("network.ip       %s", ip.nwipstr);
            cli_echof_("network.mask     %s", ip.nwmaskstr);
            cli_echof_("network.gw       %s", ip.nwgwstr);
        }
#else
        cli_echof_("network.ip       %s", ip.ip);
        cli_echof_("network.mask     %s", ip.mask);
        cli_echof_("network.gw       %s", ip.gw);
        cli_echof_("network.dns      %s", ip.dns);
        cli_echof_("network.dhcp     %s", cli_bool_to_str_(ip.dhcpen == TRUE));
#endif
    }

    rt = netmgr_conn_get(NETCONN_AUTO, NETCONN_CMD_MAC, &mac);
    if (rt == OPRT_OK) {
        cli_format_mac_(&mac, mac_text, sizeof(mac_text));
        cli_echof_("network.mac      %s", mac_text);
    }

    cli_print_wifi_info_();
}

/**
 * @brief Print a binary preview for a KV value.
 * @param[in] value binary value buffer
 * @param[in] length buffer length
 * @return none
 */
static void cli_print_kv_binary_preview_(const uint8_t *value, size_t length)
{
    char  line[CLI_LINE_SIZE] = {0};
    int   pos                 = 0;
    size_t preview_len        = (length < 16) ? length : 16;

    pos += snprintf(line + pos, sizeof(line) - pos, "value(hex)        ");
    for (size_t i = 0; i < preview_len && pos < (int)sizeof(line); i++) {
        pos += snprintf(line + pos, sizeof(line) - pos, "%02X%s", value[i], (i + 1 == preview_len) ? "" : " ");
    }

    if (length > preview_len && pos < (int)sizeof(line)) {
        (void)snprintf(line + pos, sizeof(line) - pos, " ...");
    }

    tal_cli_echo(line);
}

/**
 * @brief Check whether a KV value is printable text.
 * @param[in] value value buffer
 * @param[in] length buffer length
 * @return true if value can be shown as text, false otherwise
 */
static bool cli_kv_value_is_text_(const uint8_t *value, size_t length)
{
    size_t text_len = length;

    if (value == NULL || length == 0) {
        return false;
    }

    if (value[length - 1] == '\0') {
        text_len = length - 1;
    }

    for (size_t i = 0; i < text_len; i++) {
        if (value[i] == '\n' || value[i] == '\r' || value[i] == '\t') {
            continue;
        }
        if (value[i] < 32 || value[i] > 126) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Join a parent path and child node name.
 * @param[in] dir parent directory
 * @param[in] name child name
 * @param[out] out output path buffer
 * @param[in] out_size output path buffer size
 * @return none
 */
static void cli_fs_join_path_(const char *dir, const char *name, char *out, size_t out_size)
{
    size_t dir_len;

    if (out == NULL || out_size == 0) {
        return;
    }

    out[0] = '\0';
    if (dir == NULL || dir[0] == '\0') {
        snprintf(out, out_size, "%s", (name != NULL) ? name : "");
        return;
    }

    if (name == NULL || name[0] == '\0') {
        snprintf(out, out_size, "%s", dir);
        return;
    }

    dir_len = strlen(dir);
    if (dir_len > 0 && dir[dir_len - 1] == '/') {
        snprintf(out, out_size, "%s%s", dir, name);
    } else {
        snprintf(out, out_size, "%s/%s", dir, name);
    }
}

/**
 * @brief Implement fs_write and fs_append shared logic.
 * @param[in] path file path
 * @param[in] mode fopen mode
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cli_fs_write_impl_(const char *path, const char *mode, int argc, char *argv[])
{
    TUYA_FILE file;
    char      content[CLI_VALUE_SIZE] = {0};
    int       written;

    if (argc < 3) {
        cli_echof_("Usage: %s <file> <content...>", argv[0]);
        return;
    }

    file = claw_fopen(path, mode);
    if (file == NULL) {
        cli_echof_("ERR: claw_fopen('%s','%s') failed", path, mode);
        return;
    }

    (void)cli_join_args_(argc, argv, 2, content, sizeof(content));
    written = claw_fwrite(content, (int)strlen(content), file);
    (void)claw_fsync(file);
    (void)claw_fclose(file);

    if (written < 0) {
        cli_echof_("ERR: write failed n=%d", written);
        return;
    }

    cli_echof_("OK: wrote %d bytes to %s", written, path);
}

/**
 * @brief Report a demo switch datapoint.
 * @param[in] enabled target state
 * @return none
 */
static void cli_report_switch_state_(bool enabled)
{
    const char *payload = enabled ? "{\"1\": true}" : "{\"1\": false}";
    OPERATE_RET rt      = tuya_iot_dp_report_json(tuya_iot_client_get(), payload);

    cli_echof_("%s: sys_switch rt=%d", (rt == OPRT_OK) ? "OK" : "ERR", rt);
}

/* ---------------------------------------------------------------------------
 * Help commands
 * --------------------------------------------------------------------------- */
/**
 * @brief Show top-level CLI help.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_help(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    tal_cli_echo("=== DuckyClaw CLI ===");
    tal_cli_echo("");

    tal_cli_echo("[System]");
    cli_echof_("  %-28s %s", "sys_status", "Show device runtime status");
    cli_echof_("  %-28s %s", "sys_heap", "Show free heap/PSRAM");
    cli_echof_("  %-28s %s", "sys_thread", "Dump all thread watermark info");
    cli_echof_("  %-28s %s", "sys_uptime", "Show uptime in readable format");
    cli_echof_("  %-28s %s", "sys_tick", "Show system tick count and uptime ms");
    cli_echof_("  %-28s %s", "sys_version", "Show app, SDK, and platform version");
    cli_echof_("  %-28s %s", "sys_log_level [level]", "Get or set log level");
    cli_echof_("  %-28s %s", "sys_reboot", "Reboot device");
    cli_echof_("  %-28s %s", "sys_random [range]", "Generate random number");
    cli_echof_("  %-28s %s", "sys_timer_count", "Show active software timers");
    cli_echof_("  %-28s %s", "sys_iot_start", "Start Tuya IoT client");
    cli_echof_("  %-28s %s", "sys_iot_stop", "Stop Tuya IoT client");
    cli_echof_("  %-28s %s", "sys_iot_restart", "Restart Tuya IoT client");
    cli_echof_("  %-28s %s", "sys_iot_reset", "Unactivate/reset Tuya IoT client");
    cli_echof_("  %-28s %s", "sys_netmgr [args...]", "Pass through to netmgr CLI");
    cli_echof_("  %-28s %s", "sys_exec <cmd...>", "Execute shell command (Linux only)");
    cli_echof_("  %-28s %s", "sys_switch <on|off>", "Report demo switch datapoint");
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
    cli_echof_("  %-28s %s", "sys_wifi_info", "Show current WiFi SSID/BSSID/RSSI");
    cli_echof_("  %-28s %s", "sys_wifi_scan", "Scan nearby WiFi APs");
#endif
    tal_cli_echo("");

    tal_cli_echo("[Filesystem]");
    cli_echof_("  %-28s %s", "fs_ls [dir]", "List directory");
    cli_echof_("  %-28s %s", "fs_stat <path>", "Show exist/type/size/mode");
    cli_echof_("  %-28s %s", "fs_cat <file> [max_bytes]", "Print text file");
    cli_echof_("  %-28s %s", "fs_hexdump <file> [max_bytes]", "Hex dump file");
    cli_echof_("  %-28s %s", "fs_write <file> <content...>", "Overwrite file");
    cli_echof_("  %-28s %s", "fs_append <file> <content...>", "Append file");
    cli_echof_("  %-28s %s", "fs_rm <path>", "Remove file or directory");
    cli_echof_("  %-28s %s", "fs_mkdir <dir>", "Create directory");
    cli_echof_("  %-28s %s", "fs_mv <old> <new>", "Rename or move path");
    cli_echof_("  %-28s %s", "default root", CLI_FS_DEFAULT_PATH);
    tal_cli_echo("");

    tal_cli_echo("[KV]");
    cli_echof_("  %-28s %s", "kv_get <key>", "Read a KV value");
    cli_echof_("  %-28s %s", "kv_set <key> <value...>", "Write a string KV value");
    cli_echof_("  %-28s %s", "kv_del <key>", "Delete a KV entry");
    cli_echof_("  %-28s %s", "kv_list", "List all KV entries");
    tal_cli_echo("");

    tal_cli_echo("[Config]");
    cli_echof_("  %-28s %s", "cfg_show", "Show effective config (KV > build)");
    cli_echof_("  %-28s %s", "cfg_reset", "Clear all config KV overrides");
    cli_echof_("  %-28s %s", "cfg_set_product_id <id>", "Set Tuya product_id");
    cli_echof_("  %-28s %s", "cfg_set_auth <uuid> <authkey>", "Set Tuya uuid and authkey");
    cli_echof_("  %-28s %s", "cfg_set_ws_token <token>", "Set WebSocket token");
    cli_echof_("  %-28s %s", "cfg_set_gw_host <host>", "Set OpenClaw gateway host");
    cli_echof_("  %-28s %s", "cfg_set_gw_port <port>", "Set OpenClaw gateway port");
    cli_echof_("  %-28s %s", "cfg_set_gw_token <token>", "Set OpenClaw gateway token");
    cli_echof_("  %-28s %s", "cfg_set_device_id <id>", "Set DuckyClaw device ID");
    cli_echof_("  %-28s %s", "cfg_set_channel_mode <mode>", "Set IM channel mode (telegram|discord|feishu|weixin)");
    cli_echof_("  %-28s %s", "cfg_set_tg_token <token>", "Set Telegram token");
    cli_echof_("  %-28s %s", "cfg_set_dc_token <token>", "Set Discord token");
    cli_echof_("  %-28s %s", "cfg_set_dc_channel <id>", "Set Discord channel_id");
    cli_echof_("  %-28s %s", "cfg_set_fs_appid <id>", "Set Feishu app_id");
    cli_echof_("  %-28s %s", "cfg_set_fs_appsecret <secret>", "Set Feishu app_secret");
    cli_echof_("  %-28s %s", "cfg_set_fs_allow <csv>", "Set Feishu allow_from CSV");
    cli_echof_("  %-28s %s", "cfg_set_proxy <host> <port> [type]", "Set outbound proxy");
    cli_echof_("  %-28s %s", "cfg_clear_proxy", "Clear outbound proxy config");
    tal_cli_echo("");

    tal_cli_echo("Note: cfg_* changes take effect after reconnect or reboot.");
}

/* ---------------------------------------------------------------------------
 * System commands
 * --------------------------------------------------------------------------- */
/**
 * @brief Show device runtime status.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_status(int argc, char *argv[])
{
    TAL_LOG_LEVEL_E   log_level = TAL_LOG_LEVEL_INFO;
    char             *reason    = NULL;
    TUYA_RESET_REASON_E reset_reason;

    (void)argc;
    (void)argv;

    tal_cli_echo("--- System status ---");
    cli_echof_("system.time.ms         %llu", (unsigned long long)tal_system_get_millisecond());

    if (tal_log_get_level(&log_level) == OPRT_OK) {
        cli_echof_("log.level         %s", cli_log_level_to_str_(log_level));
    }

    reset_reason = tal_system_get_reset_reason(&reason);
    cli_echof_("reset.reason      %d (%s)", (int)reset_reason, (reason != NULL) ? reason : "unknown");

    cli_print_heap_info_();
    cli_print_network_info_();
}

/**
 * @brief Show heap information.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_heap(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    tal_cli_echo("--- Heap status ---");
    cli_print_heap_info_();
}

/**
 * @brief Dump all thread watermark information.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_thread(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    tal_cli_echo("--- Thread watermark ---");
    tal_thread_dump_watermark();
}

/**
 * @brief Show project, SDK, and platform version info.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_version(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    tal_cli_echo("--- Version info ---");
    cli_echof_("project.name      %s", PROJECT_NAME);
    cli_echof_("project.version   %s", PROJECT_VERSION);
    cli_echof_("build.date        %s", __DATE__);
    cli_echof_("build.time        %s", __TIME__);
    cli_echof_("open.version      %s", OPEN_VERSION);
    cli_echof_("open.commit       %s", OPEN_COMMIT);
    cli_echof_("platform.chip     %s", PLATFORM_CHIP);
    cli_echof_("platform.board    %s", PLATFORM_BOARD);
    cli_echof_("platform.commit   %s", PLATFORM_COMMIT);
}

/**
 * @brief Show current system tick and millisecond counters.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_tick(int argc, char *argv[])
{
    SYS_TICK_T tick_count;
    SYS_TIME_T uptime_ms;

    (void)argc;
    (void)argv;

    tick_count = tal_system_get_tick_count();
    uptime_ms  = tal_system_get_millisecond();

    cli_echof_("tick.count       %llu", (unsigned long long)tick_count);
    cli_echof_("uptime.ms        %llu", (unsigned long long)uptime_ms);
}

/**
 * @brief Get or set CLI log level.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_log_level(int argc, char *argv[])
{
    TAL_LOG_LEVEL_E level = TAL_LOG_LEVEL_INFO;
    OPERATE_RET     rt;

    if (argc < 2) {
        rt = tal_log_get_level(&level);
        if (rt != OPRT_OK) {
            cli_echof_("ERR: tal_log_get_level rt=%d", rt);
            return;
        }
        cli_echof_("log level: %s", cli_log_level_to_str_(level));
        return;
    }

    if (cli_parse_log_level_(argv[1], &level) == false) {
        tal_cli_echo("Usage: sys_log_level [err|warn|notice|info|debug|trace]");
        return;
    }

    rt = tal_log_set_level(level);
    cli_echof_("%s: sys_log_level rt=%d", (rt == OPRT_OK) ? "OK" : "ERR", rt);
}

/**
 * @brief Reboot the device.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_reboot(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    tal_cli_echo("Rebooting device...");
    tal_system_sleep(100);
    tal_system_reset();
}

/**
 * @brief Start Tuya IoT client.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_iot_start(int argc, char *argv[])
{
    OPERATE_RET rt;

    (void)argc;
    (void)argv;

    rt = tuya_iot_start(tuya_iot_client_get());
    cli_echof_("%s: sys_iot_start rt=%d", (rt == OPRT_OK) ? "OK" : "ERR", rt);
}

/**
 * @brief Stop Tuya IoT client.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_iot_stop(int argc, char *argv[])
{
    OPERATE_RET rt;

    (void)argc;
    (void)argv;

    rt = tuya_iot_stop(tuya_iot_client_get());
    cli_echof_("%s: sys_iot_stop rt=%d", (rt == OPRT_OK) ? "OK" : "ERR", rt);
}

/**
 * @brief Restart Tuya IoT client.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_iot_restart(int argc, char *argv[])
{
    OPERATE_RET rt;

    (void)argc;
    (void)argv;

    rt = tuya_iot_stop(tuya_iot_client_get());
    if (rt != OPRT_OK) {
        cli_echof_("ERR: sys_iot_restart stop rt=%d", rt);
        return;
    }

    rt = tuya_iot_start(tuya_iot_client_get());
    cli_echof_("%s: sys_iot_restart rt=%d", (rt == OPRT_OK) ? "OK" : "ERR", rt);
}

/**
 * @brief Reset Tuya IoT activation.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_iot_reset(int argc, char *argv[])
{
    OPERATE_RET rt;

    (void)argc;
    (void)argv;

    rt = tuya_iot_reset(tuya_iot_client_get());
    cli_echof_("%s: sys_iot_reset rt=%d", (rt == OPRT_OK) ? "OK" : "ERR", rt);
}

/**
 * @brief Forward arguments to the SDK netmgr CLI.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_netmgr(int argc, char *argv[])
{
    netmgr_cmd(argc, argv);
}

/**
 * @brief Execute a shell command on Linux builds.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_exec(int argc, char *argv[])
{
#if defined(PLATFORM_LINUX) && (PLATFORM_LINUX == 1)
    char command[CLI_VALUE_SIZE] = {0};
    int  status;

    if (cli_join_args_(argc, argv, 1, command, sizeof(command)) == false) {
        tal_cli_echo("Usage: sys_exec <cmd...>");
        return;
    }

    status = system(command);
    cli_echof_("sys_exec exit=%d", status);
#else
    (void)argc;
    (void)argv;
    tal_cli_echo("ERR: sys_exec is supported on PLATFORM_LINUX only");
#endif
}

/**
 * @brief Report a demo switch datapoint.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_switch(int argc, char *argv[])
{
    if (argc < 2) {
        tal_cli_echo("Usage: sys_switch <on|off>");
        return;
    }

    if (strcmp(argv[1], "on") == 0) {
        cli_report_switch_state_(true);
    } else if (strcmp(argv[1], "off") == 0) {
        cli_report_switch_state_(false);
    } else {
        tal_cli_echo("Usage: sys_switch <on|off>");
    }
}

/**
 * @brief Show uptime in human-readable format.
 */
static void cmd_sys_uptime(int argc, char *argv[])
{
    SYS_TIME_T ms;
    uint32_t   sec, min, hour, day;

    (void)argc;
    (void)argv;

    ms   = tal_system_get_millisecond();
    sec  = (uint32_t)(ms / 1000);
    day  = sec / 86400;
    hour = (sec % 86400) / 3600;
    min  = (sec % 3600) / 60;
    sec  = sec % 60;

    cli_echof_("uptime: %ud %uh %um %us (%u ms)", day, hour, min, sec, (unsigned)ms);
}

/**
 * @brief Generate a random number.
 */
static void cmd_sys_random(int argc, char *argv[])
{
    uint32_t range = 100;
    int      val;

    if (argc >= 2) {
        range = (uint32_t)strtoul(argv[1], NULL, 10);
        if (range == 0) {
            range = 100;
        }
    }

    val = tal_system_get_random(range);
    cli_echof_("random(%u) = %d", range, val);
}

/**
 * @brief Show active software timer count.
 */
static void cmd_sys_timer_count(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    cli_echof_("active sw timers: %d", tal_sw_timer_get_num());
}

#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
/**
 * @brief Show current WiFi connection details.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_wifi_info(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    tal_cli_echo("--- WiFi info ---");
    cli_print_wifi_info_();
}

/**
 * @brief Scan nearby WiFi access points.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_sys_wifi_scan(int argc, char *argv[])
{
    AP_IF_S    *ap_list = NULL;
    uint32_t    ap_num  = 0;
    OPERATE_RET rt;

    (void)argc;
    (void)argv;

    rt = tal_wifi_all_ap_scan(&ap_list, &ap_num);
    if (rt != OPRT_OK || ap_list == NULL) {
        cli_echof_("ERR: tal_wifi_all_ap_scan rt=%d", rt);
        return;
    }

    cli_echof_("Found %u APs:", (unsigned)ap_num);
    for (uint32_t i = 0; i < ap_num; i++) {
        cli_echof_("  [%2u] %-32s  ch:%2d  rssi:%d  sec:%d",
                     i + 1, ap_list[i].ssid, ap_list[i].channel,
                     ap_list[i].rssi, ap_list[i].s_len);
    }

    tal_wifi_release_ap(ap_list);
}
#endif

/* ---------------------------------------------------------------------------
 * Filesystem commands
 * --------------------------------------------------------------------------- */
/**
 * @brief List one directory.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_fs_ls(int argc, char *argv[])
{
    const char *path = (argc >= 2) ? argv[1] : CLI_FS_DEFAULT_PATH;
    TUYA_DIR    dir  = NULL;
    OPERATE_RET rt;
    uint32_t    count = 0;

    rt = claw_dir_open(path, &dir);
    if (rt != OPRT_OK || dir == NULL) {
        cli_echof_("ERR: claw_dir_open('%s') rt=%d", path, rt);
        return;
    }

    cli_echof_("Listing: %s", path);
    while (1) {
        TUYA_FILEINFO info = NULL;
        const char   *name = NULL;
        BOOL_T        is_dir = FALSE;
        BOOL_T        is_reg = FALSE;
        char          full_path[CLI_VALUE_SIZE] = {0};
        int           size;

        rt = claw_dir_read(dir, &info);
        if (rt == OPRT_EOD) {
            break;
        }
        if (rt != OPRT_OK || info == NULL) {
            cli_echof_("ERR: claw_dir_read rt=%d", rt);
            break;
        }

        (void)claw_dir_name(info, &name);
        if (name == NULL || name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
#if CLI_FS_INFO_NEED_FREE
            tal_free(info);
#endif
            continue;
        }

        (void)claw_dir_is_directory(info, &is_dir);
        (void)claw_dir_is_regular(info, &is_reg);
        cli_fs_join_path_(path, name, full_path, sizeof(full_path));
        size = is_reg ? claw_fgetsize(full_path) : -1;

        if (is_dir) {
            cli_echof_("  <dir>  %s/", name);
        } else if (is_reg) {
            cli_echof_("  %6d  %s", size, name);
        } else {
            cli_echof_("  <unk>  %s", name);
        }

        count++;
#if CLI_FS_INFO_NEED_FREE
        tal_free(info);
#endif
    }

    (void)claw_dir_close(dir);
    cli_echof_("Done. entries=%u", (unsigned)count);
}

/**
 * @brief Show file or directory metadata.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_fs_stat(int argc, char *argv[])
{
    BOOL_T      exists = FALSE;
    TUYA_DIR    dir    = NULL;
    bool        is_dir = false;
    uint32_t    mode  = 0;
    int         size;
    OPERATE_RET rt;
    OPERATE_RET mode_rt;

    if (argc < 2) {
        tal_cli_echo("Usage: fs_stat <path>");
        return;
    }

    rt = claw_fs_is_exist(argv[1], &exists);
    if (rt != OPRT_OK) {
        cli_echof_("ERR: claw_fs_is_exist('%s') rt=%d", argv[1], rt);
        return;
    }

    if (exists == FALSE) {
        cli_echof_("NOT FOUND: %s", argv[1]);
        return;
    }

    if (claw_dir_open(argv[1], &dir) == OPRT_OK && dir != NULL) {
        is_dir = true;
        (void)claw_dir_close(dir);
    }

    size    = claw_fgetsize(argv[1]);
    mode_rt = claw_fs_mode(argv[1], &mode);

    cli_echof_("path: %s", argv[1]);
    cli_echof_("type: %s", is_dir ? "dir" : "file");
    if (!is_dir && size >= 0) {
        cli_echof_("size: %d", size);
    }
    if (mode_rt == OPRT_OK) {
        cli_echof_("mode: 0x%08x", mode);
    } else {
        cli_echof_("mode: (n/a) rt=%d", mode_rt);
    }
}

/**
 * @brief Print a text file.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_fs_cat(int argc, char *argv[])
{
    const char *path;
    long        max_bytes;
    TUYA_FILE   file;
    long        total = 0;
    char        buf[128];

    if (argc < 2) {
        tal_cli_echo("Usage: fs_cat <file> [max_bytes]");
        return;
    }

    path      = argv[1];
    max_bytes = (argc >= 3) ? strtol(argv[2], NULL, 10) : CLI_DEFAULT_TEXT_LIMIT;
    if (max_bytes <= 0) {
        tal_cli_echo("ERR: max_bytes must be > 0");
        return;
    }

    file = claw_fopen(path, "r");
    if (file == NULL) {
        cli_echof_("ERR: claw_fopen('%s') failed", path);
        return;
    }

    cli_echof_("=== %s ===", path);
    while (total < max_bytes) {
        char *line = claw_fgets(buf, (int)sizeof(buf), file);
        int   len;

        if (line == NULL) {
            break;
        }

        len = (int)strlen(line);
        if (len <= 0) {
            break;
        }

        if (total + len > max_bytes) {
            buf[max_bytes - total] = '\0';
        }

        tal_cli_echo(buf);
        total += len;
        if (total >= max_bytes) {
            break;
        }
    }

    if (claw_feof(file) != 1 && total >= max_bytes) {
        cli_echof_("[truncated] %ld bytes", total);
    }

    tal_cli_echo("=============");
    (void)claw_fclose(file);
}

/**
 * @brief Print a file as hex dump.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_fs_hexdump(int argc, char *argv[])
{
    const char *path;
    long        max_bytes;
    TUYA_FILE   file;
    uint8_t     buf[16];
    long        offset = 0;

    if (argc < 2) {
        tal_cli_echo("Usage: fs_hexdump <file> [max_bytes]");
        return;
    }

    path      = argv[1];
    max_bytes = (argc >= 3) ? strtol(argv[2], NULL, 10) : CLI_DEFAULT_HEX_LIMIT;
    if (max_bytes <= 0) {
        tal_cli_echo("ERR: max_bytes must be > 0");
        return;
    }

    file = claw_fopen(path, "r");
    if (file == NULL) {
        cli_echof_("ERR: claw_fopen('%s') failed", path);
        return;
    }

    while (offset < max_bytes) {
        int  want = (int)(((max_bytes - offset) > (long)sizeof(buf)) ? sizeof(buf) : (max_bytes - offset));
        int  read_bytes;
        char line[CLI_LINE_SIZE] = {0};
        int  pos = 0;

        read_bytes = claw_fread(buf, want, file);
        if (read_bytes <= 0) {
            break;
        }

        pos += snprintf(line + pos, sizeof(line) - pos, "%08lx  ", offset);
        for (int i = 0; i < (int)sizeof(buf); i++) {
            if (i < read_bytes) {
                pos += snprintf(line + pos, sizeof(line) - pos, "%02x ", buf[i]);
            } else {
                pos += snprintf(line + pos, sizeof(line) - pos, "   ");
            }
        }
        pos += snprintf(line + pos, sizeof(line) - pos, " |");
        for (int i = 0; i < read_bytes && pos + 2 < (int)sizeof(line); i++) {
            line[pos++] = (buf[i] >= 32 && buf[i] <= 126) ? (char)buf[i] : '.';
            line[pos]   = '\0';
        }
        if (pos + 2 < (int)sizeof(line)) {
            line[pos++] = '|';
            line[pos]   = '\0';
        }

        tal_cli_echo(line);
        offset += read_bytes;
        if (read_bytes < want) {
            break;
        }
    }

    if (offset >= max_bytes) {
        cli_echof_("[truncated] %ld bytes", offset);
    }

    (void)claw_fclose(file);
}

/**
 * @brief Overwrite a file with text content.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_fs_write(int argc, char *argv[])
{
    cli_fs_write_impl_((argc >= 2) ? argv[1] : "", "w", argc, argv);
}

/**
 * @brief Append text content to a file.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_fs_append(int argc, char *argv[])
{
    cli_fs_write_impl_((argc >= 2) ? argv[1] : "", "a", argc, argv);
}

/**
 * @brief Remove one file system path.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_fs_rm(int argc, char *argv[])
{
    OPERATE_RET rt;

    if (argc < 2) {
        tal_cli_echo("Usage: fs_rm <path>");
        return;
    }

    rt = claw_fs_remove(argv[1]);
    cli_echof_("%s: fs_rm rt=%d", (rt == OPRT_OK) ? "OK" : "ERR", rt);
}

/**
 * @brief Create one directory.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_fs_mkdir(int argc, char *argv[])
{
    OPERATE_RET rt;

    if (argc < 2) {
        tal_cli_echo("Usage: fs_mkdir <dir>");
        return;
    }

    rt = claw_fs_mkdir(argv[1]);
    cli_echof_("%s: fs_mkdir rt=%d", (rt == OPRT_OK) ? "OK" : "ERR", rt);
}

/**
 * @brief Rename or move one file system path.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_fs_mv(int argc, char *argv[])
{
    OPERATE_RET rt;

    if (argc < 3) {
        tal_cli_echo("Usage: fs_mv <old> <new>");
        return;
    }

    rt = claw_fs_rename(argv[1], argv[2]);
    cli_echof_("%s: fs_mv rt=%d", (rt == OPRT_OK) ? "OK" : "ERR", rt);
}

/* ---------------------------------------------------------------------------
 * KV commands
 * --------------------------------------------------------------------------- */
/**
 * @brief Read one KV entry.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_kv_get(int argc, char *argv[])
{
    uint8_t    *value = NULL;
    size_t      length = 0;
    OPERATE_RET rt;

    if (argc < 2) {
        tal_cli_echo("Usage: kv_get <key>");
        return;
    }

    rt = tal_kv_get(argv[1], &value, &length);
    if (rt != OPRT_OK || value == NULL) {
        cli_echof_("ERR: kv_get '%s' rt=%d", argv[1], rt);
        if (value != NULL) {
            tal_kv_free(value);
        }
        return;
    }

    cli_echof_("key: %s", argv[1]);
    cli_echof_("len: %u", (unsigned)length);
    if (cli_kv_value_is_text_(value, length)) {
        cli_echof_("value: %s", (char *)value);
    } else {
        cli_print_kv_binary_preview_(value, length);
    }

    tal_kv_free(value);
}

/**
 * @brief Write one string KV entry.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_kv_set(int argc, char *argv[])
{
    char        value[CLI_VALUE_SIZE] = {0};
    OPERATE_RET rt;

    if (argc < 3) {
        tal_cli_echo("Usage: kv_set <key> <value...>");
        return;
    }

    (void)cli_join_args_(argc, argv, 2, value, sizeof(value));
    rt = tal_kv_set(argv[1], (const uint8_t *)value, strlen(value) + 1);
    cli_echof_("%s: kv_set rt=%d", (rt == OPRT_OK) ? "OK" : "ERR", rt);
}

/**
 * @brief Delete one KV entry.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_kv_del(int argc, char *argv[])
{
    OPERATE_RET rt;

    if (argc < 2) {
        tal_cli_echo("Usage: kv_del <key>");
        return;
    }

    rt = tal_kv_del(argv[1]);
    cli_echof_("%s: kv_del rt=%d", (rt == OPRT_OK) ? "OK" : "ERR", rt);
}

/**
 * @brief List all KV entries.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_kv_list(int argc, char *argv[])
{
    static char kv_arg0[] = "kv";
    static char kv_arg1[] = "list";
    static char kv_arg2[] = "/";
    char *list_argv[] = {kv_arg0, kv_arg1, kv_arg2};

    (void)argc;
    (void)argv;

    tal_kv_cmd(sizeof(list_argv) / sizeof(list_argv[0]), list_argv);
}

/* ---------------------------------------------------------------------------
 * Config commands
 * --------------------------------------------------------------------------- */
/**
 * @brief Show effective application and IM configuration.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_show(int argc, char *argv[])
{
    char                port_text[8]   = {0};
    tuya_iot_license_t  license        = {0};
    const char         *uuid_value     = NULL;
    const char         *authkey_value  = NULL;
    const char         *uuid_source    = "not set";
    const char         *authkey_source = "not set";

    (void)argc;
    (void)argv;

    tal_cli_echo("--- Effective config ---");
    tal_cli_echo("[Application]");
    cli_print_app_cfg_item_("product_id", APP_KV_PRODUCT_ID, TUYA_PRODUCT_ID, true);
    if (tuya_authorize_read(&license) == OPRT_OK) {
        if (license.uuid != NULL && license.uuid[0] != '\0') {
            uuid_value  = license.uuid;
            uuid_source = "authorize";
        }
        if (license.authkey != NULL && license.authkey[0] != '\0') {
            authkey_value  = license.authkey;
            authkey_source = "authorize";
        }
    }
    if (uuid_value == NULL && TUYA_OPENSDK_UUID[0] != '\0') {
        uuid_value  = TUYA_OPENSDK_UUID;
        uuid_source = "build";
    }
    if (authkey_value == NULL && TUYA_OPENSDK_AUTHKEY[0] != '\0') {
        authkey_value  = TUYA_OPENSDK_AUTHKEY;
        authkey_source = "build";
    }
    cli_print_cfg_value_item_("uuid", uuid_value, uuid_source, true);
    cli_print_cfg_value_item_("authkey", authkey_value, authkey_source, true);
    cli_print_app_cfg_item_("ws_token", APP_KV_WS_TOKEN, CLAW_WS_AUTH_TOKEN, true);
    cli_print_app_cfg_item_("gw_host", APP_KV_GW_HOST, OPENCLAW_GATEWAY_HOST, true);
    snprintf(port_text, sizeof(port_text), "%u", (unsigned)OPENCLAW_GATEWAY_PORT);
    cli_print_app_cfg_item_("gw_port", APP_KV_GW_PORT, port_text, false);
    cli_print_app_cfg_item_("gw_token", APP_KV_GW_TOKEN, OPENCLAW_GATEWAY_TOKEN, true);
    cli_print_app_cfg_item_("device_id", APP_KV_DEVICE_ID, DUCKYCLAW_DEVICE_ID, true);

    tal_cli_echo("[IM]");
    cli_print_im_cfg_item_("channel_mode", IM_NVS_BOT, IM_NVS_KEY_CHANNEL_MODE, IM_SECRET_CHANNEL_MODE, false);
    cli_print_im_cfg_item_("tg.token", IM_NVS_TG, IM_NVS_KEY_TG_TOKEN, IM_SECRET_TG_TOKEN, true);
    cli_print_im_cfg_item_("dc.token", IM_NVS_DC, IM_NVS_KEY_DC_TOKEN, IM_SECRET_DC_TOKEN, true);
    cli_print_im_cfg_item_("dc.channel_id", IM_NVS_DC, IM_NVS_KEY_DC_CHANNEL_ID, IM_SECRET_DC_CHANNEL_ID, true);
    cli_print_im_cfg_item_("fs.app_id", IM_NVS_FS, IM_NVS_KEY_FS_APP_ID, IM_SECRET_FS_APP_ID, true);
    cli_print_im_cfg_item_("fs.app_secret", IM_NVS_FS, IM_NVS_KEY_FS_APP_SECRET, IM_SECRET_FS_APP_SECRET, true);
    cli_print_im_cfg_item_("fs.allow_from", IM_NVS_FS, IM_NVS_KEY_FS_ALLOW_FROM, IM_SECRET_FS_ALLOW_FROM, true);
    cli_print_im_cfg_item_("proxy.host", IM_NVS_PROXY, IM_NVS_KEY_PROXY_HOST, IM_SECRET_PROXY_HOST, true);
    cli_print_im_cfg_item_("proxy.port", IM_NVS_PROXY, IM_NVS_KEY_PROXY_PORT, IM_SECRET_PROXY_PORT, false);
    cli_print_im_cfg_item_("proxy.type", IM_NVS_PROXY, IM_NVS_KEY_PROXY_TYPE, IM_SECRET_PROXY_TYPE, false);
    cli_echof_("  %-18s %s", "proxy.enabled", cli_bool_to_str_(http_proxy_is_enabled()));

    tal_cli_echo("Note: cfg_* changes take effect after reconnect or reboot.");
}

/**
 * @brief Clear all config overrides.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_reset(int argc, char *argv[])
{
    OPERATE_RET auth_rt;

    (void)argc;
    (void)argv;

    cli_clear_app_cfg_overrides_();
    cli_clear_im_cfg_overrides_();
    auth_rt = tuya_authorize_reset();
    if (auth_rt != OPRT_OK) {
        cli_echof_("ERR: cfg_reset authorize rt=%d", auth_rt);
        return;
    }
    tal_cli_echo("OK: cleared all config KV overrides (fallback to build defaults).");
}

/**
 * @brief Set product_id override.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_set_product_id(int argc, char *argv[])
{
    cli_set_app_cfg_value_(argc, argv, "cfg_set_product_id <id>", APP_KV_PRODUCT_ID, "cfg_set_product_id");
}

/**
 * @brief Set UUID and authkey together.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_set_auth(int argc, char *argv[])
{
    cli_set_authorize_pair_(argc, argv, "cfg_set_auth <uuid> <authkey>");
}

/**
 * @brief Set WebSocket token override.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_set_ws_token(int argc, char *argv[])
{
    cli_set_app_cfg_value_(argc, argv, "cfg_set_ws_token <token>", APP_KV_WS_TOKEN, "cfg_set_ws_token");
}

/**
 * @brief Set gateway host override.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_set_gw_host(int argc, char *argv[])
{
    cli_set_app_cfg_value_(argc, argv, "cfg_set_gw_host <host>", APP_KV_GW_HOST, "cfg_set_gw_host");
}

/**
 * @brief Set gateway port override.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_set_gw_port(int argc, char *argv[])
{
    long port;

    if (argc < 2) {
        tal_cli_echo("Usage: cfg_set_gw_port <port>");
        return;
    }

    port = strtol(argv[1], NULL, 10);
    if (port <= 0 || port > 65535) {
        tal_cli_echo("ERR: invalid port (1..65535)");
        return;
    }

    cli_set_app_cfg_value_(argc, argv, "cfg_set_gw_port <port>", APP_KV_GW_PORT, "cfg_set_gw_port");
}

/**
 * @brief Set gateway token override.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_set_gw_token(int argc, char *argv[])
{
    cli_set_app_cfg_value_(argc, argv, "cfg_set_gw_token <token>", APP_KV_GW_TOKEN, "cfg_set_gw_token");
}

/**
 * @brief Set device_id override.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_set_device_id(int argc, char *argv[])
{
    cli_set_app_cfg_value_(argc, argv, "cfg_set_device_id <id>", APP_KV_DEVICE_ID, "cfg_set_device_id");
}

/**
 * @brief Set active IM channel mode.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_set_channel_mode(int argc, char *argv[])
{
    OPERATE_RET rt;
    const char *mode;

    if (argc < 2) {
        tal_cli_echo("Usage: cfg_set_channel_mode <telegram|discord|feishu|weixin>");
        return;
    }

    mode = argv[1];
    if (strcmp(mode, IM_CHAN_TELEGRAM) != 0 &&
        strcmp(mode, IM_CHAN_DISCORD) != 0 &&
        strcmp(mode, IM_CHAN_FEISHU) != 0 &&
        strcmp(mode, IM_CHAN_WEIXIN) != 0) {
        tal_cli_echo("ERR: mode must be telegram | discord | feishu | weixin");
        return;
    }

    rt = im_kv_set_string(IM_NVS_BOT, IM_NVS_KEY_CHANNEL_MODE, mode);
    if (rt != OPRT_OK) {
        cli_echof_("ERR: cfg_set_channel_mode rt=%d", rt);
        return;
    }

    cli_echof_("OK: channel_mode=%s (reconnect/reboot to take effect)", mode);
}

/**
 * @brief Set Telegram bot token.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_set_tg_token(int argc, char *argv[])
{
    cli_set_im_cfg_value_(argc, argv, "cfg_set_tg_token <token>", "telegram token", telegram_set_token);
}

/**
 * @brief Set Discord bot token.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_set_dc_token(int argc, char *argv[])
{
    cli_set_im_cfg_value_(argc, argv, "cfg_set_dc_token <token>", "discord token", discord_set_token);
}

/**
 * @brief Set Discord channel_id.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_set_dc_channel(int argc, char *argv[])
{
    cli_set_im_cfg_value_(argc, argv, "cfg_set_dc_channel <channel_id>", "discord channel_id", discord_set_channel_id);
}

/**
 * @brief Set Feishu app_id.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_set_fs_appid(int argc, char *argv[])
{
    cli_set_im_cfg_value_(argc, argv, "cfg_set_fs_appid <app_id>", "feishu app_id", feishu_set_app_id);
}

/**
 * @brief Set Feishu app_secret.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_set_fs_appsecret(int argc, char *argv[])
{
    cli_set_im_cfg_value_(argc, argv, "cfg_set_fs_appsecret <app_secret>", "feishu app_secret", feishu_set_app_secret);
}

/**
 * @brief Set Feishu allow_from CSV.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_set_fs_allow(int argc, char *argv[])
{
    cli_set_im_cfg_value_(argc, argv, "cfg_set_fs_allow <csv_allow_from>", "feishu allow_from", feishu_set_allow_from);
}

/**
 * @brief Set outbound proxy configuration.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_set_proxy(int argc, char *argv[])
{
    const char *type;
    long        port;
    OPERATE_RET rt;

    if (argc < 3) {
        tal_cli_echo("Usage: cfg_set_proxy <host> <port> [type=http|https|socks5]");
        return;
    }

    port = strtol(argv[2], NULL, 10);
    if (port <= 0 || port > 65535) {
        tal_cli_echo("ERR: invalid port (1..65535)");
        return;
    }

    type = (argc >= 4) ? argv[3] : IM_SECRET_PROXY_TYPE;
    rt   = http_proxy_set(argv[1], (uint16_t)port, type);
    if (rt != OPRT_OK) {
        cli_echof_("ERR: cfg_set_proxy rt=%d", rt);
        return;
    }

    cli_echof_("OK: proxy=%s:%ld type=%s", argv[1], port, type);
}

/**
 * @brief Clear outbound proxy configuration.
 * @param[in] argc CLI argc
 * @param[in] argv CLI argv
 * @return none
 */
static void cmd_cfg_clear_proxy(int argc, char *argv[])
{
    OPERATE_RET rt;

    (void)argc;
    (void)argv;

    rt = http_proxy_clear();
    if (rt != OPRT_OK) {
        cli_echof_("ERR: cfg_clear_proxy rt=%d", rt);
        return;
    }

    tal_cli_echo("OK: proxy cleared");
}

/* ---------------------------------------------------------------------------
 * Command table
 * --------------------------------------------------------------------------- */
static cli_cmd_t s_cli_cmd[] = {
    {.name = "help",                  .help = "Show all CLI commands",                  .func = cmd_help},

    {.name = "sys_status",            .help = "Show device runtime status",             .func = cmd_sys_status},
    {.name = "sys_heap",              .help = "Show free heap and PSRAM",               .func = cmd_sys_heap},
    {.name = "sys_thread",            .help = "Dump all thread watermark info",         .func = cmd_sys_thread},
    {.name = "sys_uptime",            .help = "Show uptime in readable format",         .func = cmd_sys_uptime},
    {.name = "sys_tick",              .help = "Show system tick and uptime ms",         .func = cmd_sys_tick},
    {.name = "sys_version",           .help = "Show app, SDK, and platform version",    .func = cmd_sys_version},
    {.name = "sys_log_level",         .help = "Get or set log level",                   .func = cmd_sys_log_level},
    {.name = "sys_reboot",            .help = "Reboot device",                          .func = cmd_sys_reboot},
    {.name = "sys_random",            .help = "Generate random number",                 .func = cmd_sys_random},
    {.name = "sys_timer_count",       .help = "Show active software timers",            .func = cmd_sys_timer_count},
    {.name = "sys_iot_start",         .help = "Start Tuya IoT client",                  .func = cmd_sys_iot_start},
    {.name = "sys_iot_stop",          .help = "Stop Tuya IoT client",                   .func = cmd_sys_iot_stop},
    {.name = "sys_iot_restart",       .help = "Restart Tuya IoT client",                .func = cmd_sys_iot_restart},
    {.name = "sys_iot_reset",         .help = "Reset Tuya IoT activation",              .func = cmd_sys_iot_reset},
    {.name = "sys_netmgr",            .help = "Pass through to netmgr CLI",             .func = cmd_sys_netmgr},
    {.name = "sys_exec",              .help = "Execute shell command on Linux",         .func = cmd_sys_exec},
    {.name = "sys_switch",            .help = "Report demo switch datapoint",           .func = cmd_sys_switch},
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
    {.name = "sys_wifi_info",         .help = "Show current WiFi SSID/BSSID/RSSI",      .func = cmd_sys_wifi_info},
    {.name = "sys_wifi_scan",         .help = "Scan nearby WiFi APs",                   .func = cmd_sys_wifi_scan},
#endif

    {.name = "fs_ls",                 .help = "List directory",                         .func = cmd_fs_ls},
    {.name = "fs_stat",               .help = "Show file or directory metadata",        .func = cmd_fs_stat},
    {.name = "fs_cat",                .help = "Print text file",                        .func = cmd_fs_cat},
    {.name = "fs_hexdump",            .help = "Hex dump file",                          .func = cmd_fs_hexdump},
    {.name = "fs_write",              .help = "Overwrite file",                         .func = cmd_fs_write},
    {.name = "fs_append",             .help = "Append file",                            .func = cmd_fs_append},
    {.name = "fs_rm",                 .help = "Remove file or directory",               .func = cmd_fs_rm},
    {.name = "fs_mkdir",              .help = "Create directory",                       .func = cmd_fs_mkdir},
    {.name = "fs_mv",                 .help = "Rename or move path",                    .func = cmd_fs_mv},

    {.name = "kv_get",                .help = "Read a KV value",                        .func = cmd_kv_get},
    {.name = "kv_set",                .help = "Write a string KV value",                .func = cmd_kv_set},
    {.name = "kv_del",                .help = "Delete a KV entry",                      .func = cmd_kv_del},
    {.name = "kv_list",               .help = "List all KV entries",                    .func = cmd_kv_list},

    {.name = "cfg_show",              .help = "Show effective config",                  .func = cmd_cfg_show},
    {.name = "cfg_reset",             .help = "Clear all config KV overrides",          .func = cmd_cfg_reset},
    {.name = "cfg_set_product_id",    .help = "Set Tuya product_id",                    .func = cmd_cfg_set_product_id},
    {.name = "cfg_set_auth",          .help = "Set Tuya uuid and authkey",              .func = cmd_cfg_set_auth},
    {.name = "cfg_set_ws_token",      .help = "Set WebSocket token",                    .func = cmd_cfg_set_ws_token},
    {.name = "cfg_set_gw_host",       .help = "Set OpenClaw gateway host",              .func = cmd_cfg_set_gw_host},
    {.name = "cfg_set_gw_port",       .help = "Set OpenClaw gateway port",              .func = cmd_cfg_set_gw_port},
    {.name = "cfg_set_gw_token",      .help = "Set OpenClaw gateway token",             .func = cmd_cfg_set_gw_token},
    {.name = "cfg_set_device_id",     .help = "Set device ID",                          .func = cmd_cfg_set_device_id},
    {.name = "cfg_set_channel_mode",  .help = "Set IM channel mode (telegram|discord|feishu|weixin)",                    .func = cmd_cfg_set_channel_mode},
    {.name = "cfg_set_tg_token",      .help = "Set Telegram token",                     .func = cmd_cfg_set_tg_token},
    {.name = "cfg_set_dc_token",      .help = "Set Discord token",                      .func = cmd_cfg_set_dc_token},
    {.name = "cfg_set_dc_channel",    .help = "Set Discord channel_id",                 .func = cmd_cfg_set_dc_channel},
    {.name = "cfg_set_fs_appid",      .help = "Set Feishu app_id",                      .func = cmd_cfg_set_fs_appid},
    {.name = "cfg_set_fs_appsecret",  .help = "Set Feishu app_secret",                  .func = cmd_cfg_set_fs_appsecret},
    {.name = "cfg_set_fs_allow",      .help = "Set Feishu allow_from CSV",              .func = cmd_cfg_set_fs_allow},
    {.name = "cfg_set_proxy",         .help = "Set outbound proxy",                     .func = cmd_cfg_set_proxy},
    {.name = "cfg_clear_proxy",       .help = "Clear outbound proxy",                   .func = cmd_cfg_clear_proxy},
};

/* ---------------------------------------------------------------------------
 * Public functions
 * --------------------------------------------------------------------------- */
/**
 * @brief Register all unified CLI commands.
 * @return none
 */
void tuya_app_cli_init(void)
{
    OPERATE_RET rt = tal_cli_cmd_register(s_cli_cmd, sizeof(s_cli_cmd) / sizeof(s_cli_cmd[0]));

    if (rt != OPRT_OK) {
        PR_ERR("tal_cli_cmd_register failed: %d", rt);
    }
    PR_DEBUG("tuya_app_cli_init: tal_cli_cmd_register success");
}
