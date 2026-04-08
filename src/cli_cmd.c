/**
 * @file cli_cmd.c
 * @brief Command Line Interface (CLI) commands for Tuya IoT applications.
 *
 * This file implements a set of CLI commands for controlling and managing Tuya
 * IoT devices. It includes commands for switching device states, executing
 * system commands, managing key-value pairs, resetting and starting/stopping
 * the IoT process, and retrieving memory usage information. These commands
 * facilitate debugging, testing, and managing Tuya IoT applications directly
 * from a command line interface.
 *
 * Key functionalities provided in this file:
 * - Switching device states (on/off).
 * - Executing arbitrary system commands.
 * - Key-value pair management for device configuration.
 * - Resetting, starting, and stopping the IoT process.
 * - Retrieving current free heap memory size.
 *
 * This implementation leverages Tuya's Application Layer (TAL) APIs and IoT SDK
 * to provide a rich set of commands for device management and debugging. It is
 * designed to enhance the development and testing process of Tuya IoT
 * applications.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#include "tal_api.h"
#include "tal_cli.h"
#include "tal_fs.h"
#include "tuya_iot.h"
#include "app_config_kv.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

extern void tal_kv_cmd(int argc, char *argv[]);
extern void netmgr_cmd(int argc, char *argv[]);

#if !(defined(ENABLE_FILE_SYSTEM) && (ENABLE_FILE_SYSTEM == 1))
#define CLI_FS_INFO_NEED_FREE 1
#else
#define CLI_FS_INFO_NEED_FREE 0
#endif

static void cli_echof(const char *fmt, ...)
{
    char    line[256] = {0};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    tal_cli_echo(line);
}

static void fs_join_path(const char *dir, const char *name, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';

    if (!dir || dir[0] == '\0') {
        snprintf(out, out_size, "%s", name ? name : "");
        return;
    }
    if (!name || name[0] == '\0') {
        snprintf(out, out_size, "%s", dir);
        return;
    }

    size_t dir_len = strlen(dir);
    if (dir_len > 0 && dir[dir_len - 1] == '/') {
        snprintf(out, out_size, "%s%s", dir, name);
    } else {
        snprintf(out, out_size, "%s/%s", dir, name);
    }
}

static void fs_help(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    tal_cli_echo("Filesystem CLI (tal_fs):");
    tal_cli_echo("  fs_ls [dir]                      - List directory (default: /)");
    tal_cli_echo("  fs_stat <path>                   - Show exist/type/size/mode");
    tal_cli_echo("  fs_cat <file> [max_bytes]        - Print text file (default max: 4096)");
    tal_cli_echo("  fs_hexdump <file> [max_bytes]    - Hex dump (default max: 512)");
    tal_cli_echo("  fs_write <file> <content...>     - Overwrite file with content");
    tal_cli_echo("  fs_append <file> <content...>    - Append content to file");
    tal_cli_echo("  fs_rm <path>                     - Remove file/dir");
    tal_cli_echo("  fs_mkdir <dir>                   - Create directory");
    tal_cli_echo("  fs_mv <old> <new>                - Rename/move");
}

static void fs_ls(int argc, char *argv[])
{
    const char *path = (argc >= 2) ? argv[1] : "/";
    TUYA_DIR    dir  = NULL;

    int rt = tal_dir_open(path, &dir);
    if (rt != OPRT_OK || !dir) {
        cli_echof("ERR: tal_dir_open('%s') rt=%d", path, rt);
        return;
    }

    cli_echof("Listing: %s", path);
    uint32_t count = 0;
    while (1) {
        TUYA_FILEINFO info = NULL;
        rt                = tal_dir_read(dir, &info);
        if (rt == OPRT_EOD) {
            break;
        }
        if (rt != OPRT_OK || !info) {
            cli_echof("ERR: tal_dir_read rt=%d", rt);
            break;
        }

        const char *name = NULL;
        (void)tal_dir_name(info, &name);
        if (!name || name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
#if CLI_FS_INFO_NEED_FREE
            tal_free(info);
#endif
            continue;
        }

        BOOL_T is_dir = FALSE;
        BOOL_T is_reg = FALSE;
        (void)tal_dir_is_directory(info, &is_dir);
        (void)tal_dir_is_regular(info, &is_reg);

        char full[256] = {0};
        fs_join_path(path, name, full, sizeof(full));
        int size = is_reg ? tal_fgetsize(full) : -1;

        if (is_dir) {
            cli_echof("  <dir>  %s/", name);
        } else if (is_reg) {
            cli_echof("  %6d  %s", size, name);
        } else {
            cli_echof("  <unk>  %s", name);
        }
        count++;

#if CLI_FS_INFO_NEED_FREE
        tal_free(info);
#endif
    }

    (void)tal_dir_close(dir);
    cli_echof("Done. entries=%u", (unsigned)count);
}

static void fs_stat(int argc, char *argv[])
{
    if (argc < 2) {
        tal_cli_echo("Usage: fs_stat <path>");
        return;
    }

    const char *path = argv[1];
    BOOL_T      exist = FALSE;
    int         rt    = tal_fs_is_exist(path, &exist);
    if (rt != OPRT_OK) {
        cli_echof("ERR: tal_fs_is_exist('%s') rt=%d", path, rt);
        return;
    }
    if (!exist) {
        cli_echof("NOT FOUND: %s", path);
        return;
    }

    bool is_dir = false;
    TUYA_DIR dir = NULL;
    if (tal_dir_open(path, &dir) == OPRT_OK && dir) {
        is_dir = true;
        (void)tal_dir_close(dir);
    }

    int size = tal_fgetsize(path);
    unsigned int mode = 0;
    int mode_rt       = tal_fs_mode(path, &mode);

    cli_echof("path: %s", path);
    cli_echof("type: %s", is_dir ? "dir" : "file");
    if (!is_dir && size >= 0) {
        cli_echof("size: %d", size);
    }
    if (mode_rt == OPRT_OK) {
        cli_echof("mode: 0x%08x", mode);
    } else {
        cli_echof("mode: (n/a) rt=%d", mode_rt);
    }
}

static void fs_cat(int argc, char *argv[])
{
    if (argc < 2) {
        tal_cli_echo("Usage: fs_cat <file> [max_bytes]");
        return;
    }

    const char *path      = argv[1];
    long        max_bytes = (argc >= 3) ? strtol(argv[2], NULL, 10) : 4096;
    if (max_bytes <= 0) {
        tal_cli_echo("ERR: max_bytes must be > 0");
        return;
    }

    TUYA_FILE f = tal_fopen(path, "r");
    if (!f) {
        cli_echof("ERR: tal_fopen('%s') failed", path);
        return;
    }

    cli_echof("=== %s ===", path);
    long total = 0;
    char buf[128];
    while (total < max_bytes) {
        char *line = tal_fgets(buf, (int)sizeof(buf), f);
        if (!line) {
            break;
        }
        int len = (int)strlen(line);
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

    if (tal_feof(f) != 1 && total >= max_bytes) {
        cli_echof("[truncated] %ld bytes", total);
    }
    tal_cli_echo("=============");
    (void)tal_fclose(f);
}

static void fs_hexdump(int argc, char *argv[])
{
    if (argc < 2) {
        tal_cli_echo("Usage: fs_hexdump <file> [max_bytes]");
        return;
    }

    const char *path      = argv[1];
    long        max_bytes = (argc >= 3) ? strtol(argv[2], NULL, 10) : 512;
    if (max_bytes <= 0) {
        tal_cli_echo("ERR: max_bytes must be > 0");
        return;
    }

    TUYA_FILE f = tal_fopen(path, "r");
    if (!f) {
        cli_echof("ERR: tal_fopen('%s') failed", path);
        return;
    }

    uint8_t buf[16];
    long    off = 0;
    while (off < max_bytes) {
        int want = (int)((max_bytes - off) > (long)sizeof(buf) ? sizeof(buf) : (max_bytes - off));
        int n    = tal_fread(buf, want, f);
        if (n <= 0) {
            break;
        }

        char line[256] = {0};
        int  pos       = 0;
        pos += snprintf(line + pos, sizeof(line) - pos, "%08lx  ", off);
        for (int i = 0; i < (int)sizeof(buf); i++) {
            if (i < n) {
                pos += snprintf(line + pos, sizeof(line) - pos, "%02x ", buf[i]);
            } else {
                pos += snprintf(line + pos, sizeof(line) - pos, "   ");
            }
        }
        pos += snprintf(line + pos, sizeof(line) - pos, " |");
        for (int i = 0; i < n; i++) {
            char c = (buf[i] >= 32 && buf[i] <= 126) ? (char)buf[i] : '.';
            if (pos + 2 < (int)sizeof(line)) {
                line[pos++] = c;
                line[pos]   = '\0';
            }
        }
        if (pos + 3 < (int)sizeof(line)) {
            line[pos++] = '|';
            line[pos]   = '\0';
        }
        tal_cli_echo(line);

        off += n;
        if (n < want) {
            break;
        }
    }

    if (off >= max_bytes) {
        cli_echof("[truncated] %ld bytes", off);
    }
    (void)tal_fclose(f);
}

static void fs_write_impl(const char *path, const char *mode, int argc, char *argv[])
{
    if (argc < 3) {
        cli_echof("Usage: %s <file> <content...>", argv[0]);
        return;
    }

    TUYA_FILE f = tal_fopen(path, mode);
    if (!f) {
        cli_echof("ERR: tal_fopen('%s','%s') failed", path, mode);
        return;
    }

    char content[512] = {0};
    size_t off        = 0;
    for (int i = 2; i < argc && off + 1 < sizeof(content); i++) {
        int n = snprintf(content + off, sizeof(content) - off, "%s%s", (i == 2) ? "" : " ", argv[i]);
        if (n < 0 || (size_t)n >= sizeof(content) - off) {
            break;
        }
        off += (size_t)n;
    }

    int nwrite = tal_fwrite(content, (int)strlen(content), f);
    (void)tal_fsync(f);
    (void)tal_fclose(f);

    if (nwrite < 0) {
        cli_echof("ERR: write failed n=%d", nwrite);
        return;
    }
    cli_echof("OK: wrote %d bytes to %s", nwrite, path);
}

static void fs_write(int argc, char *argv[])
{
    fs_write_impl((argc >= 2) ? argv[1] : "", "w", argc, argv);
}

static void fs_append(int argc, char *argv[])
{
    fs_write_impl((argc >= 2) ? argv[1] : "", "a", argc, argv);
}

static void fs_rm(int argc, char *argv[])
{
    if (argc < 2) {
        tal_cli_echo("Usage: fs_rm <path>");
        return;
    }
    int rt = tal_fs_remove(argv[1]);
    cli_echof("fs_rm rt=%d", rt);
}

static void fs_mkdir(int argc, char *argv[])
{
    if (argc < 2) {
        tal_cli_echo("Usage: fs_mkdir <dir>");
        return;
    }
    int rt = tal_fs_mkdir(argv[1]);
    cli_echof("fs_mkdir rt=%d", rt);
}

static void fs_mv(int argc, char *argv[])
{
    if (argc < 3) {
        tal_cli_echo("Usage: fs_mv <old> <new>");
        return;
    }
    int rt = tal_fs_rename(argv[1], argv[2]);
    cli_echof("fs_mv rt=%d", rt);
}

/* ------------------------------------------------------------------ */
/*  Config display helper (mirrors IM serial_cli.c pattern)            */
/* ------------------------------------------------------------------ */

static void mask_str(const char *src, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!src || src[0] == '\0') { snprintf(out, out_size, "(empty)"); return; }
    size_t len = strlen(src);
    if (len <= 4) {
        snprintf(out, out_size, "****");
    } else {
        snprintf(out, out_size, "%.4s****", src);
    }
}

static void print_cfg_item(const char *label, const char *kv_key, const char *build_val, int mask)
{
    char        kv_val[128] = {0};
    const char *src         = "not set";
    const char *val         = "(empty)";

    uint8_t *buf = NULL;
    size_t   len = 0;
    if (tal_kv_get(kv_key, &buf, &len) == OPRT_OK && buf && len > 0 && ((char *)buf)[0] != '\0') {
        size_t copy = (len < sizeof(kv_val) - 1) ? len : (sizeof(kv_val) - 1);
        memcpy(kv_val, buf, copy);
        kv_val[copy] = '\0';
        src = "kv";
        val = kv_val;
    } else if (build_val && build_val[0] != '\0') {
        src = "build";
        val = build_val;
    }
    if (buf) tal_kv_free(buf);

    if (mask && strcmp(val, "(empty)") != 0) {
        char masked[64] = {0};
        mask_str(val, masked, sizeof(masked));
        cli_echof("  %-20s %s [%s]", label, masked, src);
    } else {
        cli_echof("  %-20s %s [%s]", label, val, src);
    }
}

/* ------------------------------------------------------------------ */
/*  help - show all available commands                                 */
/* ------------------------------------------------------------------ */

static void cmd_help(int argc, char *argv[]);   /* forward decl */

/* ------------------------------------------------------------------ */
/*  config_show - display all effective configuration                  */
/* ------------------------------------------------------------------ */

static void cmd_config_show(int argc, char *argv[])
{
    (void)argc; (void)argv;

    tal_cli_echo("--- App config (effective) ---");

    tal_cli_echo("[Tuya Cloud]");
    print_cfg_item("product_id", APP_KV_PRODUCT_ID, TUYA_PRODUCT_ID, 0);
    print_cfg_item("uuid", APP_KV_UUID, TUYA_OPENSDK_UUID, 1);
    print_cfg_item("authkey", APP_KV_AUTHKEY, TUYA_OPENSDK_AUTHKEY, 1);

    tal_cli_echo("[WebSocket]");
    print_cfg_item("ws_token", APP_KV_WS_TOKEN, CLAW_WS_AUTH_TOKEN, 1);

    tal_cli_echo("[OpenClaw Gateway]");
    print_cfg_item("gw_host", APP_KV_GW_HOST, OPENCLAW_GATEWAY_HOST, 0);
    {
        char port_str[8] = {0};
        snprintf(port_str, sizeof(port_str), "%u", (unsigned)OPENCLAW_GATEWAY_PORT);
        print_cfg_item("gw_port", APP_KV_GW_PORT, port_str, 0);
    }
    print_cfg_item("gw_token", APP_KV_GW_TOKEN, OPENCLAW_GATEWAY_TOKEN, 1);
    print_cfg_item("device_id", APP_KV_DEVICE_ID, DUCKYCLAW_DEVICE_ID, 0);
}

/* ------------------------------------------------------------------ */
/*  config_set_* - set individual config items via KV                  */
/* ------------------------------------------------------------------ */

static void cmd_set_product_id(int argc, char *argv[])
{
    if (argc < 2) { tal_cli_echo("Usage: set_product_id <id>"); return; }
    OPERATE_RET rt = app_kv_set_string(APP_KV_PRODUCT_ID, argv[1]);
    cli_echof("%s: set_product_id rt=%d", rt == OPRT_OK ? "OK" : "ERR", rt);
}

static void cmd_set_uuid(int argc, char *argv[])
{
    if (argc < 2) { tal_cli_echo("Usage: set_uuid <uuid>"); return; }
    OPERATE_RET rt = app_kv_set_string(APP_KV_UUID, argv[1]);
    cli_echof("%s: set_uuid rt=%d", rt == OPRT_OK ? "OK" : "ERR", rt);
}

static void cmd_set_authkey(int argc, char *argv[])
{
    if (argc < 2) { tal_cli_echo("Usage: set_authkey <authkey>"); return; }
    OPERATE_RET rt = app_kv_set_string(APP_KV_AUTHKEY, argv[1]);
    cli_echof("%s: set_authkey rt=%d", rt == OPRT_OK ? "OK" : "ERR", rt);
}

static void cmd_set_ws_token(int argc, char *argv[])
{
    if (argc < 2) { tal_cli_echo("Usage: set_ws_token <token>"); return; }
    OPERATE_RET rt = app_kv_set_string(APP_KV_WS_TOKEN, argv[1]);
    cli_echof("%s: set_ws_token rt=%d", rt == OPRT_OK ? "OK" : "ERR", rt);
}

static void cmd_set_gw_host(int argc, char *argv[])
{
    if (argc < 2) { tal_cli_echo("Usage: set_gw_host <host>"); return; }
    OPERATE_RET rt = app_kv_set_string(APP_KV_GW_HOST, argv[1]);
    cli_echof("%s: set_gw_host rt=%d", rt == OPRT_OK ? "OK" : "ERR", rt);
}

static void cmd_set_gw_port(int argc, char *argv[])
{
    if (argc < 2) { tal_cli_echo("Usage: set_gw_port <port>"); return; }
    long port = strtol(argv[1], NULL, 10);
    if (port <= 0 || port > 65535) { tal_cli_echo("ERR: invalid port (1..65535)"); return; }
    OPERATE_RET rt = app_kv_set_string(APP_KV_GW_PORT, argv[1]);
    cli_echof("%s: set_gw_port rt=%d", rt == OPRT_OK ? "OK" : "ERR", rt);
}

static void cmd_set_gw_token(int argc, char *argv[])
{
    if (argc < 2) { tal_cli_echo("Usage: set_gw_token <token>"); return; }
    OPERATE_RET rt = app_kv_set_string(APP_KV_GW_TOKEN, argv[1]);
    cli_echof("%s: set_gw_token rt=%d", rt == OPRT_OK ? "OK" : "ERR", rt);
}

static void cmd_set_device_id(int argc, char *argv[])
{
    if (argc < 2) { tal_cli_echo("Usage: set_device_id <id>"); return; }
    OPERATE_RET rt = app_kv_set_string(APP_KV_DEVICE_ID, argv[1]);
    cli_echof("%s: set_device_id rt=%d", rt == OPRT_OK ? "OK" : "ERR", rt);
}

/* ------------------------------------------------------------------ */
/*  config_clear - clear all app KV overrides                          */
/* ------------------------------------------------------------------ */

static void cmd_config_clear(int argc, char *argv[])
{
    (void)argc; (void)argv;

    static const char *const keys[] = {
        APP_KV_PRODUCT_ID, APP_KV_UUID, APP_KV_AUTHKEY,
        APP_KV_WS_TOKEN, APP_KV_GW_HOST, APP_KV_GW_PORT,
        APP_KV_GW_TOKEN, APP_KV_DEVICE_ID,
    };

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        (void)app_kv_del(keys[i]);
    }
    tal_cli_echo("OK: cleared all app config KV overrides (fallback to build defaults).");
}

/* ------------------------------------------------------------------ */
/*  list - show all KV entries (via tal_kv_cmd "list")                */
/* ------------------------------------------------------------------ */

static void cmd_list(int argc, char *argv[])
{
    (void)argc; (void)argv;
    char *list_argv[] = {"kv", "list"};
    tal_kv_cmd(2, list_argv);
}

/* ------------------------------------------------------------------ */
/*  Existing commands                                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief switch demo on/off cmd
 *
 * @param argc
 * @param argv
 * @return void
 */
static void switch_test(int argc, char *argv[])
{
    if (argc < 2) {
        PR_INFO("usge: switch <on/off>");
        return;
    }

    char bool_value[128];
    if (0 == strcmp(argv[1], "on")) {
        sprintf(bool_value, "{\"1\": true}");
    } else if (0 == strcmp(argv[1], "off")) {
        sprintf(bool_value, "{\"1\": false}");
    } else {
        PR_INFO("usge: switch <on/off>");
        return;
    }

    tuya_iot_dp_report_json(tuya_iot_client_get(), bool_value);
}

/**
 * @brief excute system cmd
 *
 * @param argc
 * @param argv
 * @return void
 */
static void system_cmd(int argc, char *argv[])
{
    char cmd[256];

    if (argc < 2) {
        PR_INFO("usge: sys <cmd>");
        return;
    }

    size_t offset = 0;

    for (int i = 1; i < argc; i++) {
        int ret = snprintf(cmd + offset, sizeof(cmd) - offset, "%s ", argv[i]);
        if (ret < 0 || offset + ret >= sizeof(cmd)) {
            break;
        }
        offset += ret;
    }

    PR_DEBUG("system %s", cmd);
    system(cmd);
}

/**
 * @brief get free heap size cmd
 *
 * @param argc
 * @param argv
 */
static void mem(int argc, char *argv[])
{
    int free_heap = 0;
    free_heap = tal_system_get_free_heap_size();
    PR_NOTICE("cur free heap: %d", free_heap);
}

/**
 * @brief reset iot to unactive/unregister
 *
 * @param argc
 * @param argv
 */
static void reset(int argc, char *argv[])
{
    tuya_iot_reset(tuya_iot_client_get());
}

/**
 * @brief reset iot to unactive/unregister
 *
 * @param argc
 * @param argv
 */
static void start(int argc, char *argv[])
{
    tuya_iot_start(tuya_iot_client_get());
}

/**
 * @brief stop iot
 *
 * @param argc
 * @param argv
 */
static void stop(int argc, char *argv[])
{
    tuya_iot_stop(tuya_iot_client_get());
}

/**
 * @brief cli cmd list
 *
 */
static cli_cmd_t s_cli_cmd[] = {
    /* --- General --- */
    {.name = "help",            .func = cmd_help,          .help = "Show all commands"},
    {.name = "list",            .func = cmd_list,          .help = "List all KV entries"},
    {.name = "config_show",     .func = cmd_config_show,   .help = "Show app config (kv/build)"},
    {.name = "config_clear",    .func = cmd_config_clear,  .help = "Clear all app config KV overrides"},
    /* --- Config set --- */
    {.name = "set_product_id",  .func = cmd_set_product_id,  .help = "Set Tuya product_id"},
    {.name = "set_uuid",        .func = cmd_set_uuid,        .help = "Set Tuya uuid"},
    {.name = "set_authkey",     .func = cmd_set_authkey,     .help = "Set Tuya authkey"},
    {.name = "set_ws_token",    .func = cmd_set_ws_token,    .help = "Set WebSocket auth token"},
    {.name = "set_gw_host",     .func = cmd_set_gw_host,     .help = "Set OpenClaw gateway host"},
    {.name = "set_gw_port",     .func = cmd_set_gw_port,     .help = "Set OpenClaw gateway port"},
    {.name = "set_gw_token",    .func = cmd_set_gw_token,    .help = "Set OpenClaw gateway token"},
    {.name = "set_device_id",   .func = cmd_set_device_id,   .help = "Set DuckyClaw device ID"},
    /* --- System --- */
    {.name = "switch", .func = switch_test, .help = "switch test"},
    {.name = "kv", .func = tal_kv_cmd, .help = "kv operations (get/set/del/list)"},
    {.name = "sys", .func = system_cmd, .help = "Execute system command"},
    {.name = "reset", .func = reset, .help = "Reset IoT (unactivate)"},
    {.name = "stop", .func = stop, .help = "Stop IoT process"},
    {.name = "start", .func = start, .help = "Start IoT process"},
    {.name = "mem", .func = mem, .help = "Show free heap memory"},
    {.name = "netmgr", .func = netmgr_cmd, .help = "Network manager cmd"},
    /* --- Filesystem --- */
    {.name = "fs_help", .func = fs_help, .help = "Filesystem help"},
    {.name = "fs_ls", .func = fs_ls, .help = "List directory"},
    {.name = "fs_stat", .func = fs_stat, .help = "Stat path"},
    {.name = "fs_cat", .func = fs_cat, .help = "Print text file"},
    {.name = "fs_hexdump", .func = fs_hexdump, .help = "Hexdump file"},
    {.name = "fs_write", .func = fs_write, .help = "Write file (overwrite)"},
    {.name = "fs_append", .func = fs_append, .help = "Write file (append)"},
    {.name = "fs_rm", .func = fs_rm, .help = "Remove file/dir"},
    {.name = "fs_mkdir", .func = fs_mkdir, .help = "Make directory"},
    {.name = "fs_mv", .func = fs_mv, .help = "Rename/move"},
};

/* help implementation (needs s_cli_cmd to be defined) */
static void cmd_help(int argc, char *argv[])
{
    (void)argc; (void)argv;

    tal_cli_echo("=== DuckyClaw CLI Commands ===");

    tal_cli_echo("");
    tal_cli_echo("[General]");
    cli_echof("  %-20s %s", "help", "Show this help message");
    cli_echof("  %-20s %s", "list", "List all KV storage entries");
    cli_echof("  %-20s %s", "config_show", "Show effective app config (kv overrides / build defaults)");
    cli_echof("  %-20s %s", "config_clear", "Clear all app config KV overrides");

    tal_cli_echo("");
    tal_cli_echo("[Config Set]");
    cli_echof("  %-20s %s", "set_product_id <id>", "Set Tuya product ID");
    cli_echof("  %-20s %s", "set_uuid <uuid>", "Set Tuya OpenSDK UUID");
    cli_echof("  %-20s %s", "set_authkey <key>", "Set Tuya OpenSDK auth key");
    cli_echof("  %-20s %s", "set_ws_token <token>", "Set WebSocket authentication token");
    cli_echof("  %-20s %s", "set_gw_host <host>", "Set OpenClaw gateway host");
    cli_echof("  %-20s %s", "set_gw_port <port>", "Set OpenClaw gateway port");
    cli_echof("  %-20s %s", "set_gw_token <token>", "Set OpenClaw gateway token");
    cli_echof("  %-20s %s", "set_device_id <id>", "Set DuckyClaw device ID");

    tal_cli_echo("");
    tal_cli_echo("[System]");
    cli_echof("  %-20s %s", "switch <on|off>", "Switch demo on/off");
    cli_echof("  %-20s %s", "kv <cmd> [args]", "KV operations (get/set/del/list)");
    cli_echof("  %-20s %s", "sys <cmd>", "Execute system shell command");
    cli_echof("  %-20s %s", "reset", "Reset IoT (unactivate device)");
    cli_echof("  %-20s %s", "stop", "Stop IoT process");
    cli_echof("  %-20s %s", "start", "Start IoT process");
    cli_echof("  %-20s %s", "mem", "Show current free heap memory");
    cli_echof("  %-20s %s", "netmgr", "Network manager command");

    tal_cli_echo("");
    tal_cli_echo("[Filesystem]");
    cli_echof("  %-20s %s", "fs_help", "Show filesystem command details");
    cli_echof("  %-20s %s", "fs_ls [dir]", "List directory (default: /)");
    cli_echof("  %-20s %s", "fs_stat <path>", "Show file/dir info");
    cli_echof("  %-20s %s", "fs_cat <file> [max]", "Print text file");
    cli_echof("  %-20s %s", "fs_hexdump <file>", "Hex dump of file");
    cli_echof("  %-20s %s", "fs_write <f> <text>", "Write file (overwrite)");
    cli_echof("  %-20s %s", "fs_append <f> <text>", "Append to file");
    cli_echof("  %-20s %s", "fs_rm <path>", "Remove file/dir");
    cli_echof("  %-20s %s", "fs_mkdir <dir>", "Create directory");
    cli_echof("  %-20s %s", "fs_mv <old> <new>", "Rename/move");

    tal_cli_echo("");
    tal_cli_echo("[IM] (type im_help for IM-specific commands)");
    tal_cli_echo("");
    tal_cli_echo("Note: config changes take effect after reboot.");
}

/**
 * @brief
 *
 */
void tuya_app_cli_init(void)
{
    tal_cli_cmd_register(s_cli_cmd, sizeof(s_cli_cmd) / sizeof(s_cli_cmd[0]));
}