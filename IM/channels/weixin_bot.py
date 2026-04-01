#!/usr/bin/env python3
"""
Weixin iLink Bot — Python prototype for MCU logic validation.

Implements the same state machine as the C MCU target:
  1. QR login (print URL to terminal; user scans with WeChat on PC browser)
  2. Save bot_token + allowFrom user to JSON state file
  3. Long-poll getupdates loop with retry/backoff/session-pause
  4. allowFrom authorization check per message
  5. context_token persistence per user
  6. Text reply via sendmessage

Usage:
    pip install requests
    python weixin_bot.py --login          # first run: QR login
    python weixin_bot.py                  # subsequent runs: use saved token
    python weixin_bot.py --token <tok>    # inject token directly (skip QR)

See README_python_prototype.md for full documentation.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import random
import sys
import time
from typing import Callable, Dict, List, Optional

try:
    import requests
except ImportError:
    print("ERROR: 'requests' library not found. Install it with:\n  pip install requests")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Constants — mirrors MCU #define values
# ---------------------------------------------------------------------------

DEFAULT_BASE_URL = "https://ilinkai.weixin.qq.com"
CDN_BASE_URL = "https://novac2c.cdn.weixin.qq.com/c2c"

BOT_TYPE = "3"
CHANNEL_VERSION = "1.0.3"

LONG_POLL_TIMEOUT_S = 35        # getupdates long-poll timeout (client-side)
QR_POLL_TIMEOUT_S = 35          # get_qrcode_status long-poll timeout
QR_MAX_REFRESH = 3              # max QR code refresh attempts before giving up
LOGIN_TOTAL_TIMEOUT_S = 480     # 8 minutes

MAX_CONSECUTIVE_FAILURES = 3    # trigger backoff after N failures
BACKOFF_DELAY_S = 30            # sleep duration on repeated failures
RETRY_DELAY_S = 2               # sleep duration on single failure
SESSION_PAUSE_S = 3600          # 1 hour pause after errcode -14

SESSION_EXPIRED_ERRCODE = -14

DEFAULT_STATE_FILE = "weixin_state.json"

# ---------------------------------------------------------------------------
# HTTP helpers
# ---------------------------------------------------------------------------

def _random_wechat_uin() -> str:
    """X-WECHAT-UIN: base64(str(random_uint32))."""
    uint32 = random.randint(0, 0xFFFF_FFFF)
    return base64.b64encode(str(uint32).encode()).decode()


def _build_headers(token: Optional[str], body: str) -> Dict[str, str]:
    headers: Dict[str, str] = {
        "Content-Type": "application/json",
        "AuthorizationType": "ilink_bot_token",
        "Content-Length": str(len(body.encode("utf-8"))),
        "X-WECHAT-UIN": _random_wechat_uin(),
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    return headers


def _base_info() -> dict:
    return {"channel_version": CHANNEL_VERSION}


def _api_post(base_url: str, token: Optional[str], endpoint: str,
              payload: dict, timeout: int = 15) -> dict:
    url = f"{base_url.rstrip('/')}/{endpoint.lstrip('/')}"
    body = json.dumps(payload, ensure_ascii=False)
    headers = _build_headers(token, body)
    r = requests.post(url, headers=headers, data=body.encode("utf-8"), timeout=timeout)
    r.raise_for_status()
    return r.json()


# ---------------------------------------------------------------------------
# State persistence
# ---------------------------------------------------------------------------

class State:
    """Simple JSON-file-backed key-value store (mirrors NVS on MCU)."""

    def __init__(self, path: str) -> None:
        self.path = path
        self._data: dict = {}
        self._load()

    def _load(self) -> None:
        if os.path.exists(self.path):
            try:
                with open(self.path, "r", encoding="utf-8") as f:
                    self._data = json.load(f)
            except Exception:
                self._data = {}

    def save(self) -> None:
        with open(self.path, "w", encoding="utf-8") as f:
            json.dump(self._data, f, indent=2, ensure_ascii=False)

    def get(self, key: str, default=None):
        return self._data.get(key, default)

    def set(self, key: str, value) -> None:
        self._data[key] = value
        self.save()


# ---------------------------------------------------------------------------
# QR Login
# ---------------------------------------------------------------------------

def qr_login(base_url: str, state: State) -> bool:
    """
    Full QR login flow.
    Prints the QR URL to the terminal; the user opens it in a browser and scans
    with WeChat — exactly the same flow an MCU would use via UART output.

    Returns True on success, False on failure.
    """
    # Step 1: request QR code
    url = f"{base_url.rstrip('/')}/ilink/bot/get_bot_qrcode?bot_type={BOT_TYPE}"
    print("[LOGIN] 正在从服务器获取二维码...")
    try:
        r = requests.get(url, timeout=15)
        r.raise_for_status()
        data = r.json()
    except Exception as e:
        print(f"[LOGIN] 获取二维码失败: {e}")
        return False

    qrcode: str = data["qrcode"]
    qrcode_url: str = data["qrcode_img_content"]

    print()
    print("=" * 64)
    print("  请在 PC 浏览器中打开以下链接，然后用微信 App 扫码：")
    print()
    print(f"  {qrcode_url}")
    print()
    print("  (这与 MCU 通过串口打印 URL 给用户扫码的方式完全一致)")
    print("=" * 64)
    print()

    # Step 2: poll for status
    refresh_count = 0
    scanned_notified = False
    deadline = time.time() + LOGIN_TOTAL_TIMEOUT_S

    while time.time() < deadline:
        try:
            status_url = (
                f"{base_url.rstrip('/')}/ilink/bot/get_qrcode_status"
                f"?qrcode={requests.utils.quote(qrcode)}"
            )
            r = requests.get(
                status_url,
                headers={"iLink-App-ClientVersion": "1"},
                timeout=QR_POLL_TIMEOUT_S + 5,
            )
            r.raise_for_status()
            resp = r.json()
        except requests.Timeout:
            # Normal long-poll timeout — retry
            continue
        except Exception as e:
            print(f"\n[LOGIN] 状态查询异常: {e}")
            time.sleep(2)
            continue

        status = resp.get("status", "wait")

        if status == "wait":
            print(".", end="", flush=True)

        elif status == "scaned":
            if not scanned_notified:
                print("\n[LOGIN] 已扫码，请在手机微信上点击「确认」...")
                scanned_notified = True

        elif status == "expired":
            refresh_count += 1
            if refresh_count > QR_MAX_REFRESH:
                print(f"\n[LOGIN] 二维码连续过期 {QR_MAX_REFRESH} 次，登录中止。请重新运行。")
                return False
            print(f"\n[LOGIN] 二维码已过期，正在刷新 ({refresh_count}/{QR_MAX_REFRESH})...")
            try:
                r2 = requests.get(url, timeout=15)
                r2.raise_for_status()
                d2 = r2.json()
                qrcode = d2["qrcode"]
                qrcode_url = d2["qrcode_img_content"]
                scanned_notified = False
                print(f"[LOGIN] 新二维码链接:\n  {qrcode_url}\n")
            except Exception as e:
                print(f"[LOGIN] 刷新二维码失败: {e}")
                return False

        elif status == "confirmed":
            bot_token: str = resp.get("bot_token", "")
            ilink_bot_id: str = resp.get("ilink_bot_id", "")
            ilink_user_id: str = resp.get("ilink_user_id", "")
            srv_base_url: str = resp.get("baseurl") or base_url

            if not bot_token or not ilink_bot_id:
                print("\n[LOGIN] 登录失败：服务器未返回 bot_token 或 ilink_bot_id")
                return False

            print(f"\n[LOGIN] ✅ 登录成功！")
            print(f"  ilink_bot_id  = {ilink_bot_id}")
            print(f"  ilink_user_id = {ilink_user_id}")
            print(f"  base_url      = {srv_base_url}")

            state.set("bot_token", bot_token)
            state.set("base_url", srv_base_url)
            state.set("ilink_bot_id", ilink_bot_id)
            # allowFrom: the user who scanned the QR is automatically authorized
            allow_from: List[str] = state.get("allow_from", [])
            if ilink_user_id and ilink_user_id not in allow_from:
                allow_from.append(ilink_user_id)
                state.set("allow_from", allow_from)
                print(f"  → {ilink_user_id} 已加入 allowFrom 授权列表")
            return True

        time.sleep(1)

    print("\n[LOGIN] 等待超时（8 分钟），请重新运行并扫码。")
    return False


# ---------------------------------------------------------------------------
# API wrappers
# ---------------------------------------------------------------------------

def api_get_updates(base_url: str, token: str, get_updates_buf: str) -> dict:
    payload = {
        "get_updates_buf": get_updates_buf,
        "base_info": _base_info(),
    }
    try:
        return _api_post(base_url, token, "ilink/bot/getupdates", payload,
                         timeout=LONG_POLL_TIMEOUT_S + 5)
    except requests.Timeout:
        # Normal for long-poll — return empty response so loop continues
        return {"ret": 0, "msgs": [], "get_updates_buf": get_updates_buf}


def api_send_message(base_url: str, token: str, to: str, text: str,
                     context_token: Optional[str] = None) -> None:
    client_id = f"weixin-py-{int(time.time() * 1000)}-{random.randint(0, 0xFFFF):04x}"
    payload: dict = {
        "msg": {
            "from_user_id": "",
            "to_user_id": to,
            "client_id": client_id,
            "message_type": 2,       # BOT
            "message_state": 2,      # FINISH
            "item_list": [{"type": 1, "text_item": {"text": text}}],
            **({"context_token": context_token} if context_token else {}),
        },
        "base_info": _base_info(),
    }
    _api_post(base_url, token, "ilink/bot/sendmessage", payload, timeout=15)


# ---------------------------------------------------------------------------
# Message parsing
# ---------------------------------------------------------------------------

def extract_text_body(item_list: Optional[list]) -> str:
    """
    Extract the primary text from item_list.
    Priority: TEXT item > VOICE item with STT text.
    Mirrors bodyFromItemList() in inbound.ts (simplified, no ref_msg).
    """
    for item in item_list or []:
        t = item.get("type")
        if t == 1:  # TEXT
            return item.get("text_item", {}).get("text", "") or ""
        if t == 3:  # VOICE — use STT text if available
            stt = item.get("voice_item", {}).get("text", "")
            if stt:
                return stt
    return ""


# ---------------------------------------------------------------------------
# Default message handler (echo bot)
# Replace this function with your own logic.
# ---------------------------------------------------------------------------

def default_on_message(from_user: str, text: str, context_token: Optional[str],
                       base_url: str, token: str) -> None:
    """
    Default handler: echo the received text back to the sender.
    This is the function to replace with your AI/business logic.
    """
    if text:
        reply = f"收到: {text}"
    else:
        reply = "（收到了一条媒体消息，暂不支持回复）"

    preview = text[:60] + ("…" if len(text) > 60 else "")
    print(f"[MSG] from={from_user} body={preview!r}")
    print(f"[MSG]    → reply={reply!r}")

    api_send_message(base_url, token, from_user, reply, context_token)


# ---------------------------------------------------------------------------
# Monitor loop — mirrors weixin_monitor_task() on MCU
# ---------------------------------------------------------------------------

MessageHandler = Callable[[str, str, Optional[str], str, str], None]


def monitor(state: State, handler: MessageHandler = default_on_message) -> None:
    """
    Long-poll loop. Runs forever until KeyboardInterrupt.

    State read on entry; get_updates_buf and context_tokens are persisted
    after every successful poll so the loop can resume after a restart.
    """
    base_url: str = state.get("base_url", DEFAULT_BASE_URL)
    token: str = state.get("bot_token", "")
    allow_from: List[str] = state.get("allow_from", [])
    context_tokens: Dict[str, str] = state.get("context_tokens", {})
    get_updates_buf: str = state.get("get_updates_buf", "")

    print(f"\n[MONITOR] 启动")
    print(f"  base_url      = {base_url}")
    print(f"  get_updates_buf 长度 = {len(get_updates_buf)} bytes")
    if allow_from:
        print(f"  allow_from    = {allow_from}")
    else:
        print("  ⚠️  allow_from 为空 — 所有入站消息将被丢弃！请先完成 QR 登录。")
    print()

    consecutive_failures = 0
    session_paused_until: float = 0.0
    next_timeout_s = LONG_POLL_TIMEOUT_S

    while True:
        # --- session pause check ---
        now = time.time()
        if now < session_paused_until:
            remaining = int(session_paused_until - now)
            print(f"[MONITOR] session 暂停中，剩余 {remaining}s ...")
            time.sleep(min(remaining, 60))
            continue

        # --- getupdates ---
        try:
            resp = api_get_updates(base_url, token, get_updates_buf)
        except Exception as e:
            consecutive_failures += 1
            print(f"[MONITOR] getupdates 异常 ({consecutive_failures}/{MAX_CONSECUTIVE_FAILURES}): {e}")
            if consecutive_failures >= MAX_CONSECUTIVE_FAILURES:
                consecutive_failures = 0
                time.sleep(BACKOFF_DELAY_S)
            else:
                time.sleep(RETRY_DELAY_S)
            continue

        # --- update dynamic poll timeout ---
        srv_timeout = resp.get("longpolling_timeout_ms", 0)
        if srv_timeout and srv_timeout > 0:
            next_timeout_s = srv_timeout // 1000

        # --- error handling ---
        ret = resp.get("ret", 0)
        errcode = resp.get("errcode", 0)

        if ret != 0 or errcode != 0:
            if errcode == SESSION_EXPIRED_ERRCODE or ret == SESSION_EXPIRED_ERRCODE:
                session_paused_until = time.time() + SESSION_PAUSE_S
                print(
                    f"[MONITOR] session 已过期 (errcode={errcode})，"
                    f"暂停 {SESSION_PAUSE_S // 60} 分钟后自动恢复"
                )
                consecutive_failures = 0
                continue

            consecutive_failures += 1
            errmsg = resp.get("errmsg", "")
            print(
                f"[MONITOR] API 错误 ret={ret} errcode={errcode} errmsg={errmsg!r} "
                f"({consecutive_failures}/{MAX_CONSECUTIVE_FAILURES})"
            )
            if consecutive_failures >= MAX_CONSECUTIVE_FAILURES:
                consecutive_failures = 0
                time.sleep(BACKOFF_DELAY_S)
            else:
                time.sleep(RETRY_DELAY_S)
            continue

        consecutive_failures = 0

        # --- persist get_updates_buf ---
        new_buf: str = resp.get("get_updates_buf", "")
        if new_buf:
            get_updates_buf = new_buf
            state.set("get_updates_buf", get_updates_buf)

        # --- process messages ---
        for msg in resp.get("msgs") or []:
            from_user: str = msg.get("from_user_id", "")
            ctx_token: Optional[str] = msg.get("context_token") or None

            # allowFrom authorization (mirrors pairing.ts logic on MCU)
            if allow_from and from_user not in allow_from:
                print(f"[MONITOR] 忽略未授权用户: {from_user}")
                continue

            # persist context_token (mirrors .context-tokens.json on disk / NVS)
            if ctx_token:
                context_tokens[from_user] = ctx_token
                state.set("context_tokens", context_tokens)

            text = extract_text_body(msg.get("item_list"))

            try:
                handler(from_user, text, ctx_token, base_url, token)
            except Exception as e:
                print(f"[MONITOR] 消息处理异常 from={from_user}: {e}")


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Weixin iLink Bot — Python prototype (mirrors MCU C implementation)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python weixin_bot.py --login          # QR login (first time)
  python weixin_bot.py                  # use saved token
  python weixin_bot.py --token <tok>    # inject token directly
  python weixin_bot.py --login --base-url https://ilinkai.weixin.qq.com
        """,
    )
    parser.add_argument("--login", action="store_true",
                        help="强制重新进行 QR 登录（更新 token 和 allowFrom）")
    parser.add_argument("--token", metavar="BOT_TOKEN",
                        help="直接指定 bot_token，跳过 QR 登录")
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL,
                        help=f"API base URL（默认: {DEFAULT_BASE_URL}）")
    parser.add_argument("--state", default=DEFAULT_STATE_FILE, metavar="FILE",
                        help=f"状态文件路径（默认: {DEFAULT_STATE_FILE}）")
    args = parser.parse_args()

    state = State(args.state)

    # Inject token from CLI arg (highest priority)
    if args.token:
        state.set("bot_token", args.token)
        if not state.get("base_url"):
            state.set("base_url", args.base_url)

    # QR login
    if args.login or not state.get("bot_token"):
        print("[SETUP] 开始 QR 登录流程...")
        ok = qr_login(args.base_url, state)
        if not ok:
            print("[ERROR] 登录失败，请重试")
            sys.exit(1)

    if not state.get("bot_token"):
        print("[ERROR] 没有有效的 bot_token，请使用 --login 参数重新登录")
        sys.exit(1)

    # Start long-poll loop
    try:
        monitor(state)
    except KeyboardInterrupt:
        print("\n[MONITOR] 已停止")


if __name__ == "__main__":
    main()
