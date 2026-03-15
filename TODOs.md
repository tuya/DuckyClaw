## 软件 TODOs
- [x] 将 your_chat_bot 项目迁移重命名为 duckyclaw
- [x] 使用涂鸦云作为主代理
- [x] 添加 WhatsApp、Telegram、飞书等消息支持
#### AI 引擎 
- [x] Agent Loop

#### 设备 MCP 工具
- [x] `CRON` 设备工具 MCP
- [x] `FILE` 设备工具 MCP
- [x] `EXEC` 设备工具 MCP : 树莓派命令注入
- [x] `IoT 设备控制` 工具 MCP : 控制涂鸦连接设备网络上的设备
#### 存储
- [x] SD 卡文件系统操作支持
- [x] 本地 `Agent.txt` 和 `memory.txt` 用于自定义代理提示和本地记忆支持
#### 其他
- [ ] 音频 ASR 输入、消息应用输入、网关切换机制
- [ ] CLI 更多设置 [涂鸦认证、消息配置]
- [ ] **T5AI 开发板表情显示问题**: `ai_ui_icon_font.c` 中 `sg_emo_list` 只定义了7个表情，但 AI 可能返回27种表情，导致大部分表情无法正确显示。需扩展表情映射表并增加 `FONT_EMO_ICON_MAX_NUM` 限制。

## 文档 TODOs
- [x] 免费许可证指南
- [ ] 涂鸦 T5 DuckyClaw 快速入门
- [ ] ESP32 DuckyClaw
- [ ] 树莓派 DuckyClaw 快速入门
- [ ] 设备 MCP 快速指南
### TuyaOpen.ai
- [ ] DuckyClaw 落地页
- [ ] DuckyClaw 主 README 中/英文

## 非安全代码审查清单 (2026-03-14)

### 🔴 高危 (High) - 6项

- [ ] NS-001: app_im 发送路径在 NULL 输入时可能崩溃
	- Location: src/app_im.c:153, src/app_im.c:160, src/app_im.c:168
	- 问题: `s_channel` 和 `message` 在 `app_im_bot_send_message` 中直接用于 `strncpy` 和 `strlen`，无 NULL 检查。
	- 修复: 在函数入口添加 `RETURN_IF_FALSE(s_channel && message)`。

- [ ] NS-003: 流式 DATA 处理缺少防御性检查和分块边界控制
	- Location: src/ducky_claw_chat.c:198-208 (DATA case)
	- 问题: `__ai_chat_handle_event` 的 DATA case 直接访问 `text->datalen`/`text->data`，未检查 `text` 是否为 NULL。对比 START case 有 NULL 检查，DATA case 缺少。
	- 修复: 添加 NULL 检查和 chunk size 限制。

- [ ] NS-004: 缺少 START 时流式 STOP/DATA 可能使用 NULL 流缓冲区
	- Location: src/ducky_claw_chat.c:209-214 (STOP case), src/ducky_claw_chat.c:207 (DATA case)
	- 问题: STOP 和 DATA 分支直接使用 `stream_data` 指针，若未收到 START 事件则指针为 NULL。
	- 修复: 在 DATA/STOP 分支添加 `if (!stream_data) return;`。

- [ ] NS-009: cron 共享状态在多线程访问时无锁保护
	- Location: cron_service/cron_service.c (s_jobs 全局变量，cron_process_due_jobs:272-323, cron_add_job:389-413, cron_remove_job:418-435)
	- 问题: `cron_process_due_jobs` 在线程中访问 `s_jobs`/`s_job_count`，而 `cron_add_job`/`cron_remove_job` 等 API 修改这些全局变量，均无锁保护。
	- 修复: 引入 `tal_mutex` 保护所有共享状态访问。

- [ ] NS-010: fs_cat 截断逻辑可能索引越界
	- Location: src/cli_cmd.c:195, src/cli_cmd.c:200
	- 问题: 局部数组 `buf[128]` (第195行) 但使用 `buf[max_bytes - total]` 索引 (第200行)，当 `max_bytes > 128` 时发生越界写入。默认 `max_bytes=4096`，风险较高。
	- 修复: 使用 `sizeof(buf)` 或限制 `max_bytes <= sizeof(buf)`。

- [ ] NS-011: 升级回调解引用 cJSON 字段时无空指针/类型检查
	- Location: src/tuya_app_main.c:89-98
	- 问题: `user_upgrade_notify_on` 中多处 `cJSON_GetObjectItem(...)->valueint` 和 `->valuestring` 直接解引用，无 NULL 检查。
	- 修复: 检查每个 cJSON 指针是否为 NULL 再访问成员。

### 🟡 中危 (Medium) - 4项

- [ ] NS-002: app_im 在推送失败时可能泄漏出站内容
	- Location: src/app_im.c:178
	- 问题: `message_bus_push_outbound(&out)` 返回值未检查，`out.content` 已分配的内存在 push 失败时无法释放。
	- 修复: 检查返回值，失败时 `tal_free(out.content)`。

- [ ] NS-005: 入站消息在 agent loop 提前 continue 分支中内存泄漏
	- Location: agent/agent_loop.c:100, agent/agent_loop.c:110, agent/agent_loop.c:132
	- 问题: `agent_loop_task` 函数中第 100、110、132 行的 `continue` 在 `in.content` 已分配后跳过末尾的 `tal_free(in.content)`，导致内存泄漏。
	- 修复: 在每个 `continue` 前释放 `in.content`，或改用 `goto cleanup` 模式。

- [ ] NS-006: agent 历史记录读写同步不一致
	- Location: agent/agent_loop.c:114, agent/agent_loop.c:119
	- 问题: 写入历史时使用 `s_history_mutex`，但读取 `s_history` 构建上下文时未加锁。
	- 修复: 在读取 `s_history` 时也加锁保护。

- [ ] NS-008: agent 提示历史追加存在偏移计算问题
	- Location: agent/agent_loop.c:117, agent/agent_loop.c:125, agent/agent_loop.c:134
	- 问题: 多处 `system_prompt_len += snprintf(...)` 累加返回值，`snprintf` 返回"如果缓冲区足够大将写入的字符数"而非实际写入数，累加后可能超出实际位置。
	- 修复: 添加返回值检查，确保 offset 不会超出缓冲区范围。

### 🟢 低危 (Low) - 2项

- [ ] NS-007: 提示缓冲区偏移在截断后可能超出缓冲区大小
	- Location: agent/context_builder.c:114-187 (多处 `off += snprintf(...)` 模式)
	- 问题: `off += snprintf(...)` 可能导致 offset 超出缓冲区大小，造成越界写入。实际触发风险较低（缓冲区为 32KB）。
	- 修复: 可选：改用 `off = (off < size) ? off + snprintf(...) : off` 提高健壮性。

- [ ] NS-012: cron_list 字符串构建器可能错误处理截断偏移
	- Location: tools/tool_cron.c:220, tools/tool_cron.c:226, tools/tool_cron.c:233
	- 问题: `__tool_cron_list` 函数中 `off += snprintf(...)` 模式可能导致截断后输出不完整。有 `off < buf_size - 1` 检查，风险较低。
	- 修复: 添加边界检查 `if (off >= sizeof(buf)) break;`。

### 建议合并修复

- NS-007 + NS-008 + NS-012: 相同的 `snprintf` offset 模式
- NS-003 + NS-004: 同一 stream 事件处理函数的健壮性问题
- NS-001 + NS-002: 同一函数 `app_im_bot_send_message` 的问题

## 安全与深度代码审查清单 (2026-03-14)

### 🔴 严重 (Critical) - 2项

- [ ] SC-001: 命令执行无任何安全限制
	- Location: tools/tool_exec.c:321-325 (`__is_allowed_safe_command` 函数)
	- 问题: `__is_allowed_safe_command()` 只要 `cmd != NULL` 就返回 true，任何命令都可以执行，包括 `rm -rf /` 等破坏性命令。
	- 影响: 远程代码执行风险，系统完全失守。
	- 修复: 实现命令白名单机制，或禁止危险命令 (`rm`, `dd`, `mkfs`, `shutdown` 等)。

- [ ] SC-002: 线程安全违规导致数据竞争
	- Location: agent/agent_loop.c:114-126
	- 问题: `agent_loop_task` 访问 `s_history_json` 时未持有锁，但 `build_current_context` 函数加锁修改同一对象。
	- 影响: 可能读取损坏的 JSON 数据或程序崩溃。
	- 修复: 在 `agent_loop_task` 中读取 `s_history_json` 时加锁保护。

### 🟠 高危 (High) - 9项

- [ ] SH-001: 静态缓冲区内存泄漏
	- Location: src/ducky_claw_chat.c:156 (`__ai_chat_handle_event` 函数内 `static char *stream_data`)
	- 问题: `static stream_data` 动态分配后从未释放，模块销毁时无法释放该内存。静态变量只分配一次，不会重复泄漏。
	- 修复: 添加清理函数或在模块销毁时释放内存。

- [ ] SH-003: cJSON 空指针解引用
	- Location: src/tuya_app_main.c:89-98 (`user_upgrade_notify_on` 函数)
	- 问题: `cJSON_GetObjectItem` 返回值未检查 NULL，缺少字段时崩溃。
	- 修复: 添加空指针检查。

- [ ] SH-004: WebSocket 无身份验证机制
	- Location: gateway/ws_server.c:492-519 (`ws_accept_client_locked` 函数)
	- 问题: 任何客户端都可连接并发送消息到 AI 代理，无任何身份验证。
	- 修复: 在握手阶段验证 token 或 API key。

- [ ] SH-005: WebSocket 无 TLS 加密
	- Location: gateway/ws_server.c, gateway/chat.py
	- 问题: 使用明文 `ws://` 传输，无 `wss://` 支持。
	- 修复: 添加 TLS/SSL 加密支持。

- [ ] SH-006: session_clear 缺少完整路径遍历保护
	- Location: memory/session_manager.c:205-229 (`session_clear` 函数)
	- 问题: 只检查 `/` 和 `\`，未检查 `..` 序列，可能导致任意文件访问。
	- 修复: 添加统一的 `chat_id` 验证函数，检查路径遍历字符。

- [ ] SH-007: 整数下溢导致缓冲区溢出
	- Location: memory/memory_manager.c:188-222 (`memory_read_recent` 函数)
	- 问题: `size - off - 1` 当 `off >= size - 1` 时可能发生无符号整数下溢。
	- 风险评估: 循环条件 `off < size - 1` 提供了部分保护，实际触发风险较低。
	- 修复: 添加边界检查 `if (off >= size - 1) return offset;`。

- [ ] SH-008: Linux 平台路径验证失效
	- Location: tools/tool_files.c:44-55 (`__validate_path` 函数), tools/tool_files.h:51-54
	- 问题: `CLAW_FS_ROOT_PATH` 在 Linux 定义为空字符串 `""`，导致 `strncmp(path, "/", 0)` 总是返回 0，路径验证完全失效。任何以 `/` 开头的路径都被认为是有效的。
	- 影响: Linux 平台沙箱完全失效，可访问任意文件。
	- 修复: 在 Linux 平台定义非空根路径，或使用 `realpath()` 规范化路径。

- [ ] SH-009: 初始化失败时资源泄漏
	- Location: agent/agent_loop.c:183-222 (`agent_loop_init` 函数)
	- 问题: 初始化过程中后续步骤失败时，之前分配的资源 (`s_total_prompt`, `s_history_json`) 未释放。
	- 修复: 使用 `goto cleanup` 模式或检查每步返回值后释放已分配资源。

### 🟡 中危 (Medium) - 8项

- [ ] SM-001: 全局变量缺少同步保护
	- Location: src/app_im.c:38-39
	- 问题: `s_channel` 和 `s_chat_id` 在多线程环境中被并发访问，无锁保护。
	- 修复: 使用互斥锁或原子操作。

- [ ] SM-002: 服务停止时线程可能持有锁被删除
	- Location: gateway/ws_server.c:733-761 (`ws_server_stop` 函数)
	- 问题: `tal_thread_delete` 强制删除线程，可能在线程持有锁时发生。
	- 修复: 使用条件变量让线程自行退出。

- [ ] SM-003: chat_id 静默截断
	- Location: gateway/ws_server.c:290, gateway/ws_server.c:330 (`ws_do_handshake_locked` 函数)
	- 问题: 超长 `chat_id` 被 `snprintf` 静默截断，无警告。
	- 修复: 检查长度并返回错误或警告。

- [ ] SM-004: 握手失败无 HTTP 错误响应
	- Location: gateway/ws_server.c:291-294 (`ws_do_handshake_locked` 函数)
	- 问题: WebSocket 握手失败时只返回错误码，未发送 HTTP 400 Bad Request 响应，客户端直接关闭连接。
	- 修复: 发送适当的 HTTP 错误响应。

- [ ] SM-005: session_manager 行缓冲区过小
	- Location: memory/session_manager.c:137 (`session_get_history_json` 函数)
	- 问题: `char line[2048]` 可能不足以容纳长消息或 base64 数据。
	- 修复: 增大缓冲区或实现动态分配。

- [ ] SM-006: JSON 截断时返回成功
	- Location: memory/session_manager.c:180-183
	- 问题: `strncpy` 截断 JSON 时返回 OPRT_OK，调用方无法区分。
	- 修复: 检查 `strlen(json_str) >= size` 并返回错误。

- [ ] SM-007: 缺少线程安全保护
	- Location: memory/memory_manager.c, memory/session_manager.c
	- 问题: 文件操作无互斥锁保护，多任务环境可能数据损坏。
	- 修复: 添加互斥锁保护文件操作。

- [ ] SM-008: 子进程超时处理可优化
	- Location: tools/tool_exec.c:133-141 (`__pi_system_exec_capture` 函数)
	- 问题: 超时时已正确调用 `waitpid` 回收子进程，不存在僵尸进程风险。但超时路径和正常路径使用不同的返回方式，代码可维护性可优化。
	- 修复: 可选：统一超时和正常退出路径的处理逻辑。

### 🟢 低危 (Low) - 5项

- [ ] SL-001: Kconfig 拼写错误
	- Location: agent/Kconfig:3-17
	- 问题: `DUCKY_CALW_*` 应为 `DUCKY_CLAW_*`，导致配置无效。
	- 修复: 修正拼写为 `DUCKY_CLAW_*`。

- [ ] SL-002: 缺少清理函数
	- Location: agent/agent_loop.c
	- 问题: 无 `agent_loop_deinit()` 函数释放资源。
	- 修复: 添加模块销毁函数。

- [ ] SL-003: 返回值类型不一致
	- Location: agent/agent_loop.c:151-177 (`agent_loop_start_cb` 函数)
	- 问题: 函数声明返回 `int`，但返回 `OPRT_MALLOC_FAILED` 枚举值。
	- 修复: 统一返回值类型。

- [ ] SL-004: Python 客户端缺少错误处理
	- Location: gateway/chat.py
	- 问题: 无超时设置、无连接失败处理、硬编码 IP 地址。
	- 修复: 添加错误处理和命令行参数支持。

- [ ] SL-005: 目录创建失败未检查
	- Location: memory/memory_manager.c:97-100, memory/session_manager.c:70-72
	- 问题: `claw_fs_mkdir` 返回值未检查。
	- 修复: 检查返回值并处理错误。