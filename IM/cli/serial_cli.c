#include "cli/serial_cli.h"

#include "im_config.h"

#include "bus/message_bus.h"

#include "channels/discord_bot.h"
#include "channels/feishu_bot.h"
#include "channels/telegram_bot.h"
#include "proxy/http_proxy.h"
#include "tal_cli.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "im.cli";
static bool        s_cli_inited;

static void cli_echof(const char *fmt, ...)
{
    char    line[256] = {0};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    tal_cli_echo(line);
}

static void mask_copy(const char *src, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    if (!src || src[0] == '\0') {
        snprintf(out, out_size, "(empty)");
        return;
    }

    size_t len = strlen(src);
    if (len <= 4) {
        snprintf(out, out_size, "****");
    } else {
        snprintf(out, out_size, "%.4s****", src);
    }
}

static void print_config_item(const char *label, const char *ns, const char *key, const char *build_val, bool mask)
{
    char        kv_val[128] = {0};
    const char *src         = "not set";
    const char *val         = "(empty)";

    if (im_kv_get_string(ns, key, kv_val, sizeof(kv_val)) == OPRT_OK && kv_val[0] != '\0') {
        src = "kv";
        val = kv_val;
    } else if (build_val && build_val[0] != '\0') {
        src = "build";
        val = build_val;
    }

    if (mask && strcmp(val, "(empty)") != 0) {
        char masked[64] = {0};
        mask_copy(val, masked, sizeof(masked));
        cli_echof("%-16s %s [%s]", label, masked, src);
    } else {
        cli_echof("%-16s %s [%s]", label, val, src);
    }
}

static void cmd_im_help(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    tal_cli_echo("IM CLI commands (usage + description):");
    cli_echof("  %-24s %s", "im_config_show", "Show effective IM config (kv/build)");
    cli_echof("  %-24s %s", "im_config_reset", "Clear IM KV overrides (fallback to build defaults)");
    cli_echof("  %-24s %s", "im_set_channel_mode <telegram|discord|feishu>", "Set active bot channel mode");
    cli_echof("  %-24s %s", "im_set_tg_token <token>", "Set Telegram bot token");
    cli_echof("  %-24s %s", "im_set_dc_token <token>", "Set Discord bot token");
    cli_echof("  %-24s %s", "im_set_dc_channel <channel_id>", "Set Discord default channel_id");
    cli_echof("  %-24s %s", "im_set_fs_appid <app_id>", "Set Feishu app_id");
    cli_echof("  %-24s %s", "im_set_fs_appsecret <app_secret>", "Set Feishu app_secret");
    cli_echof("  %-24s %s", "im_set_fs_allow <csv_allow_from>", "Set Feishu allow_from CSV (sender open_id allowlist)");
    cli_echof("  %-24s %s", "im_set_proxy <host> <port> [type=http|https|socks5]", "Configure outbound proxy");
    cli_echof("  %-24s %s", "im_clear_proxy", "Clear proxy configuration");
    tal_cli_echo("Note: channel_mode change takes effect after reconnect/reboot.");
}

static void cmd_im_config_show(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    tal_cli_echo("--- IM config (effective) ---");

    print_config_item("channel_mode", IM_NVS_BOT, IM_NVS_KEY_CHANNEL_MODE, IM_SECRET_CHANNEL_MODE, false);

    print_config_item("tg.token", IM_NVS_TG, IM_NVS_KEY_TG_TOKEN, IM_SECRET_TG_TOKEN, true);

    print_config_item("dc.token", IM_NVS_DC, IM_NVS_KEY_DC_TOKEN, IM_SECRET_DC_TOKEN, true);
    print_config_item("dc.channel_id", IM_NVS_DC, IM_NVS_KEY_DC_CHANNEL_ID, IM_SECRET_DC_CHANNEL_ID, false);

    print_config_item("fs.app_id", IM_NVS_FS, IM_NVS_KEY_FS_APP_ID, IM_SECRET_FS_APP_ID, false);
    print_config_item("fs.app_secret", IM_NVS_FS, IM_NVS_KEY_FS_APP_SECRET, IM_SECRET_FS_APP_SECRET, true);
    print_config_item("fs.allow_from", IM_NVS_FS, IM_NVS_KEY_FS_ALLOW_FROM, IM_SECRET_FS_ALLOW_FROM, false);

    print_config_item("proxy.host", IM_NVS_PROXY, IM_NVS_KEY_PROXY_HOST, IM_SECRET_PROXY_HOST, false);
    print_config_item("proxy.port", IM_NVS_PROXY, IM_NVS_KEY_PROXY_PORT, IM_SECRET_PROXY_PORT, false);
    print_config_item("proxy.type", IM_NVS_PROXY, IM_NVS_KEY_PROXY_TYPE, IM_SECRET_PROXY_TYPE, false);

    cli_echof("%-16s %s", "proxy.enabled", http_proxy_is_enabled() ? "true" : "false");
}

static void cmd_im_config_reset(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    (void)im_kv_del(IM_NVS_BOT, IM_NVS_KEY_CHANNEL_MODE);

    (void)im_kv_del(IM_NVS_TG, IM_NVS_KEY_TG_TOKEN);

    (void)im_kv_del(IM_NVS_DC, IM_NVS_KEY_DC_TOKEN);
    (void)im_kv_del(IM_NVS_DC, IM_NVS_KEY_DC_CHANNEL_ID);
    (void)im_kv_del(IM_NVS_DC, IM_NVS_KEY_DC_LAST_MSG_ID);

    (void)im_kv_del(IM_NVS_FS, IM_NVS_KEY_FS_APP_ID);
    (void)im_kv_del(IM_NVS_FS, IM_NVS_KEY_FS_APP_SECRET);
    (void)im_kv_del(IM_NVS_FS, IM_NVS_KEY_FS_ALLOW_FROM);

    (void)http_proxy_clear();

    tal_cli_echo("OK: cleared IM KV overrides (fallback to build defaults).");
}

static void cmd_im_set_channel_mode(int argc, char *argv[])
{
    if (argc < 2) {
        tal_cli_echo("Usage: im_set_channel_mode <telegram|discord|feishu>");
        return;
    }
    const char *mode = argv[1];
    if (strcmp(mode, IM_CHAN_TELEGRAM) != 0 && strcmp(mode, IM_CHAN_DISCORD) != 0 && strcmp(mode, IM_CHAN_FEISHU) != 0) {
        tal_cli_echo("Invalid mode. Use: telegram | discord | feishu");
        return;
    }

    OPERATE_RET rt = im_kv_set_string(IM_NVS_BOT, IM_NVS_KEY_CHANNEL_MODE, mode);
    if (rt != OPRT_OK) {
        cli_echof("ERR: set channel_mode failed, rt=%d", rt);
        return;
    }
    cli_echof("OK: channel_mode=%s (reconnect/reboot to take effect)", mode);
}

static void cmd_im_set_tg_token(int argc, char *argv[])
{
    if (argc < 2) {
        tal_cli_echo("Usage: im_set_tg_token <token>");
        return;
    }
    OPERATE_RET rt = telegram_set_token(argv[1]);
    if (rt != OPRT_OK) {
        cli_echof("ERR: telegram_set_token failed, rt=%d", rt);
        return;
    }
    tal_cli_echo("OK: telegram token saved to KV");
}

static void cmd_im_set_dc_token(int argc, char *argv[])
{
    if (argc < 2) {
        tal_cli_echo("Usage: im_set_dc_token <token>");
        return;
    }
    OPERATE_RET rt = discord_set_token(argv[1]);
    if (rt != OPRT_OK) {
        cli_echof("ERR: discord_set_token failed, rt=%d", rt);
        return;
    }
    tal_cli_echo("OK: discord token saved to KV");
}

static void cmd_im_set_dc_channel(int argc, char *argv[])
{
    if (argc < 2) {
        tal_cli_echo("Usage: im_set_dc_channel <channel_id>");
        return;
    }
    OPERATE_RET rt = discord_set_channel_id(argv[1]);
    if (rt != OPRT_OK) {
        cli_echof("ERR: discord_set_channel_id failed, rt=%d", rt);
        return;
    }
    tal_cli_echo("OK: discord channel_id saved to KV");
}

static void cmd_im_set_fs_appid(int argc, char *argv[])
{
    if (argc < 2) {
        tal_cli_echo("Usage: im_set_fs_appid <app_id>");
        return;
    }
    OPERATE_RET rt = feishu_set_app_id(argv[1]);
    if (rt != OPRT_OK) {
        cli_echof("ERR: feishu_set_app_id failed, rt=%d", rt);
        return;
    }
    tal_cli_echo("OK: feishu app_id saved to KV");
}

static void cmd_im_set_fs_appsecret(int argc, char *argv[])
{
    if (argc < 2) {
        tal_cli_echo("Usage: im_set_fs_appsecret <app_secret>");
        return;
    }
    OPERATE_RET rt = feishu_set_app_secret(argv[1]);
    if (rt != OPRT_OK) {
        cli_echof("ERR: feishu_set_app_secret failed, rt=%d", rt);
        return;
    }
    tal_cli_echo("OK: feishu app_secret saved to KV");
}

static void cmd_im_set_fs_allow(int argc, char *argv[])
{
    if (argc < 2) {
        tal_cli_echo("Usage: im_set_fs_allow <csv_allow_from>");
        return;
    }
    OPERATE_RET rt = feishu_set_allow_from(argv[1]);
    if (rt != OPRT_OK) {
        cli_echof("ERR: feishu_set_allow_from failed, rt=%d", rt);
        return;
    }
    tal_cli_echo("OK: feishu allow_from saved to KV");
}

static void cmd_im_set_proxy(int argc, char *argv[])
{
    if (argc < 3) {
        tal_cli_echo("Usage: im_set_proxy <host> <port> [type=http|https|socks5]");
        return;
    }

    const char *host = argv[1];
    long        port = strtol(argv[2], NULL, 10);
    const char *type = (argc >= 4) ? argv[3] : IM_SECRET_PROXY_TYPE;

    if (port <= 0 || port > 65535) {
        tal_cli_echo("ERR: invalid port (1..65535)");
        return;
    }

    OPERATE_RET rt = http_proxy_set(host, (uint16_t)port, type);
    if (rt != OPRT_OK) {
        cli_echof("ERR: http_proxy_set failed, rt=%d", rt);
        return;
    }
    cli_echof("OK: proxy=%s:%ld type=%s", host, port, type);
}

static void cmd_im_clear_proxy(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    OPERATE_RET rt = http_proxy_clear();
    if (rt != OPRT_OK) {
        cli_echof("ERR: http_proxy_clear failed, rt=%d", rt);
        return;
    }
    tal_cli_echo("OK: proxy cleared");
}

static const cli_cmd_t s_im_cli_cmds[] = {
    {.name = "im_help", .help = "Show IM CLI help", .func = cmd_im_help},
    {.name = "im_config_show", .help = "Show IM config (kv/build)", .func = cmd_im_config_show},
    {.name = "im_config_reset", .help = "Clear IM KV overrides", .func = cmd_im_config_reset},
    {.name = "im_set_channel_mode", .help = "Set bot channel_mode", .func = cmd_im_set_channel_mode},
    {.name = "im_set_tg_token", .help = "Set Telegram bot token", .func = cmd_im_set_tg_token},
    {.name = "im_set_dc_token", .help = "Set Discord bot token", .func = cmd_im_set_dc_token},
    {.name = "im_set_dc_channel", .help = "Set Discord channel_id", .func = cmd_im_set_dc_channel},
    {.name = "im_set_fs_appid", .help = "Set Feishu app_id", .func = cmd_im_set_fs_appid},
    {.name = "im_set_fs_appsecret", .help = "Set Feishu app_secret", .func = cmd_im_set_fs_appsecret},
    {.name = "im_set_fs_allow", .help = "Set Feishu allow_from CSV", .func = cmd_im_set_fs_allow},
    {.name = "im_set_proxy", .help = "Set proxy host/port/type", .func = cmd_im_set_proxy},
    {.name = "im_clear_proxy", .help = "Clear proxy configuration", .func = cmd_im_clear_proxy},
};

OPERATE_RET serial_cli_init(void)
{
    if (s_cli_inited) {
        return OPRT_OK;
    }

    OPERATE_RET rt = tal_cli_cmd_register(s_im_cli_cmds, sizeof(s_im_cli_cmds) / sizeof(s_im_cli_cmds[0]));
    if (rt != OPRT_OK) {
        IM_LOGE(TAG, "tal_cli_cmd_register failed: %d", rt);
        return rt;
    }

    s_cli_inited = true;
    IM_LOGI(TAG, "IM serial cli initialized, cmds=%u",
            (unsigned)(sizeof(s_im_cli_cmds) / sizeof(s_im_cli_cmds[0])));
    return OPRT_OK;
}
