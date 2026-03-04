# DuckyClaw: Hardware-Driven OpenClaw Project

DuckyClaw is a hardware oriented version of the OpenClaw. The Ideas is to explore the physical hardware with the powerful concept of OpenClaw agents.

## Deployable to Hardware

| Category | Models/Platforms |
|----------|-----------------|
| **MCUs** | - Tuya T5AI Module<br>- ESP32s |
| **SoCs** | - Raspberry Pi 4/5/CM4/CM5<br>- Linux ARM SoCs (Qualcomm/Rockchip/Allwinner etc...) |
| **PCs**  | - Linux Ubuntu |

---




This project is build on top of the the TuyaOpen C SDK, which offers flexable cross ARM Cortex-M and ARM Cortex-A, and event x64 PC deployments. And also offers wide varierity ready-to-use hardware drivers and API. Makeing integreating new hardware and peferials as easy as building blocks. (Sensors, Displays, Speaker-Mic Audio, Cameras, to IoT cloud integreations)



![GitHub Repo Banner](https://images.tuyacn.com/fe-static/docs/img/210f532a-0bb1-4ca5-9037-f5488958a709.jpg)


**Your autonomous AI companion.**
> Simplify Hardware Integration, Unlock Infinite Control Possibilities


<!-- 
> [!NOTE]
> **Developer Free License Available**: Unlimited AI Tokens. No more stress on token usage. -->


> [!WARNING]
> **🚧 Under Active Development** - This project is in heavy development and things will break. Running it now may spoil the experience we're building for you. Please visit or open Issues if you encountered any problem.






AI agents today are powerful but complex, expensive to run, and heavy to set up. Tiny Claw believes AI should be **simple, affordable, and truly personal**, like having your own Codsworth or AYLA as a helpful friend. It achieves this by being a **native framework built from scratch** with a tiny core, plugin architecture, self-improving memory, and smart routing that tiers queries to cut costs. The result is an **autonomous, self-improving, self-learning, and self-configuring personal AI companion** that grows with you over time.

Think of the **personal computer revolution**. Computers were once reserved for governments, military, and large corporations. Having one meant building it yourself or spending serious money. Then Apple came along and made them personal and accessible to everyone. Tiny Claw does the same for AI agents.





## ❓ Why  DuckyClaw?
Most AI agent frameworks are powerful but cumbersome. They often come with costly subscriptions, complicated setups, and are layered on top of other frameworks and APIs. DuckyClaw offers a different path.

Built on a commercial-grade device–cloud AI agent foundation, DuckyClaw uniquely fuses on-device agents with the power of the cloud.

With a single TuyaOpen Key, you gain seamless access to the Tuya Cloud Platform and its next-generation device–cloud AI agents.

It’s lightweight and effortlessly deployable to almost any edge hardware. From WIFI connected MCUs to System on Chip ARM, to PC Ubuntus.


| | DuckyClaw 🐜 | OpenClaw / MimiClaw / others |
|---|---|---|
| **Architecture** | Hardware-oriented OpenClaw on TuyaOpen C SDK; device–cloud fusion | OpenClaw: Node.js, 24/7 desktop/server agent. MimiClaw: bare-metal C on ESP32-S3 only. Others: framework stacks (Pi, Claude Code, etc.) |
| **Deployment** | MCUs (Tuya T5AI, ESP32s), SoCs (RPi 4/5, ARM Linux), PC (Ubuntu); one codebase | OpenClaw: Mac mini, Pi, VPS. MimiClaw: single $5 ESP32-S3. Others: server/desktop only, no edge MCU |
| **Runtime** | TuyaOpen C; cross ARM Cortex-M, ARM Cortex-A, x64; no Node on MCU | OpenClaw: Node.js. MimiClaw: no OS, no Node. Others: Node.js 22+, pnpm, full OS |
| **Device–cloud** | One TuyaOpen key → Tuya Cloud Platform + device; built-in device-cloud AI | OpenClaw: self-hosted, optional API subs. MimiClaw: local-only (SOUL.md, USER.md, MEMORY.md). Others: cloud-only or DIY |
| **Channels** | Telegram, Discord, Feishu via unified IM component (message bus, proxy, TLS) | OpenClaw: WhatsApp, Telegram, Discord, iMessage, Slack, etc. MimiClaw: Telegram, WebSocket, serial CLI. Others: terminal or separate UI |
| **Memory** | MEMORY.md + daily notes (YYYY-MM-DD.md); session manager; ported from MimiClaw-style store | OpenClaw: 24/7 persistent context, Obsidian/Raycast. MimiClaw: on-device MEMORY.md. Others: flat conversation history |
| **Tools** | CRON, FILE, IoT device control (Tuya); EXEC (RPi) planned; MCP-style device tools | OpenClaw: browser automation, cron, ClawHub skills. MimiClaw: ReAct + web search, time, OTA, GPIO. Others: ad-hoc or single provider |
| **✨ Setup** | Single TuyaOpen key; pick board (T5AI/ESP32/RPi/Linux), configure IM tokens | OpenClaw: Gateway, OAuth, multi-step. MimiClaw: flash firmware, API keys. Others: complex onboarding, many deps |
| **✨ Cost** | Free Unlimited Tokens! | OpenClaw/MimiClaw: bring your own Claude/OpenAI. Others: Claude Pro/Max ($20–200/mo) or API-heavy |
| **✨ Device IoT Control** | Native device and IoT control, controlling other device in the ecosystem (over TuyaOpen/embedded protocols) | ❌ (typically no built-in device control) |
| **✨ Voice (ASR) Input** |  Hardware voice input (ASR) supported on select boards | ❌ (voice input not natively supported) |



## 💡 Philosophy

DuckyClaw is inspired by the vision of making personal AI accessible, practical, and genuinely helpful—rooted not in corporate tools, but in the idea of an always-there companion that intuitively fits into your daily life and hardware. The project’s name and mascot of Cute Duck from Tuya, embrace the spirit of resourceful, adaptive creatures of Claw.

**Why a duck and a claw?** Ducks are omnipresent, adaptable, and thrive in almost any environment—just like DuckyClaw’s software, which you can deploy from microcontrollers to desktop Linux. The "claw" symbolizes precision, dexterity, and direct command over both your real-world devices and digital agents. With TuyaOpen, hardware integration is much more flexible, allowing you to easily connect a wide range of hardware features and capabilities directly into your agent. This project isn’t about replacing people—it’s about building a dependable, always-learning, always-improving companion you fully control with real-life hardware interfaces.



### Core Principles

- **Personal-first, not enterprise.** DuckyClaw is made for individuals and makers. Your workflows and day-to-day life come first, no bureaucracy or corporate bloat.
- **Lean core, plugin power.** Everything outside the essential C core—channels, providers, tools—is a plugin you can swap and extend.
- **Self-tuning and adaptive.** Learns via real episode memory and periodic self-review, with memory fading that helps it stay relevant.
- **IoT control memory.** DuckyClaw remembers device states, actions, and preferences as part of IoT_MEMORY.md, allowing it to build a rich contextual history of your devices and interactions. Of how you control other devices in the Tuya ecosystem.  This lets the agent adapt to your automation routines, device quirks, and personal preferences over time—enabling smarter, more intuitive IoT control that gets better the more you use it.
- **Config by conversation.** No thick config files: set up and modify your agent simply by chatting, both on hardware and in the cloud.
- **Distinctive personality.** Each instance has its own configuration and optional Hardware layer for characterful, helpful responses. 
- **Builds natively on C/TuyaOpen.** No Node, no Python frameworks glued on. Built from scratch for the boards and platforms you actually use.
- **Rapid to start.** Requires just a TuyaOpen key. Choose models and providers as needed—switch between them with the messaging interface anytime.
- **Unified model access.** With a single TuyaOpen API Access Key, you can tap into the latest models from GPT, Claude, Deepseek, Qwen, and more—choose the best model for each task, all seamlessly hosted and managed by Tuya.
- **Device and Cloud AI Agents.** DuckyClaw is natively built to support both agents that run locally on your device (“device agents”) and those that operate in Claw like manner the cloud (“cloud agents”). This hybrid architecture enables on-device actions, voice, and IoT control while also providing access to advanced cloud intelligence, remote coordination, and online skills, MCPs. You can configure, blend, or move workloads fluidly between device and cloud—giving you the flexibility to optimize for speed, privacy, and power as you choose.

## ✨ Features

- **Unified Messaging Input:**  
  Seamless integration with WhatsApp, Telegram, Feishu, and more through a unified proxy IM interface.

- **Device–Cloud Hybrid AI Agent:**  
  Centralized AI agent powered by TuyaOpen, enabling both on-device and cloud-based actions with the Claw mechanism.

- **IoT Device Control:**  
  Native control and management of Tuya IoT smart devices directly from the agent.

- **Music Playback & Audio Features:**  
  Built-in music player. Audio input via Automatic Speech Recognition (ASR) *(in progress)*.

- **MCP Device Tools:**  
  - **CRON Tool MCP:** Scheduled device tasks and heartbeating.
  - **FILE Tool MCP:** File operations and management on supported devices.
  - **IoT Device Control Tool MCP:** Advanced management for Tuya-connected devices.
  - **EXEC Tool MCP:** Remote code execution/injection on Raspberry Pi

- **Additional Capabilities:**  
  - SD Card storage support
  - Persistent memory via local `Agent.txt` and `memory.txt`
  - Flexible Gateway switching for device/cloud agent handoff
  - Simple CLI for Tuya authentication and messaging setup 
  



## 🏛️ Architecture

![Architecture](https://images.tuyacn.com/fe-static/docs/img/5f408897-2151-4f2f-9f22-216391a14f58.jpg)

The DuckyClaw architecture combines local device agents and cloud agents under a unified system. At its core, it uses the TuyaOpen AI-Agent framework to handle messaging, automation, and control. Local hardware (like Raspberry Pi, ESP32, or Linux devices) runs its own Claw like loop agent, able to communicate directly with your devices via IoT and hardware interfaces. 


## 🚀 Quick Start [TODO]

### Install
[TODO]

### Run
[TODO]

### Development
- Tuya T5 MCU Guide: [TODO]
- Raspberry Pi Guide: [TODO]
- Linux: [TODO]
- ESP32 MCU Guide: [TODO]


## 🔌 Plugin/Skills Development

- Skills development guide: [TODO]

## 📁 Project Structure
- [TODO]


## 🐛 Issues

Please report any issues and bugs by [creating a new issue here](https://github.com/tuya/DuckyClaw/issues), also make sure you're reporting an issue that doesn't exist. Any help to improve the project would be appreciated. Thanks! 🙏✨

## 🙏🙏🙏 Follow us

Like this project? Leave a star! ⭐⭐⭐⭐⭐

This project wouldn't be possible without the amazing TuyaOpen Open Source AI-Agent framework. It makes AI-Agent integreation really easy 
<a href="https://github.com/tuya/TuyaOpen/" target="_blank">
  <img src="https://img.shields.io/badge/TuyaOpen%20Repo-Visit-blue?logo=github" alt="TuyaOpen Repo"/>
</a>


## 📃 License

This project is licensed under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).

## 👥 Credits
- [TuyaOpen](https://github.com/tuya/TuyaOpen/) - 
This project is build on top of this amazing hardware AIoT OS call TuyaOpen.
- [OpenClaw](https://github.com/openclaw/openclaw) — original idea and inspiration
- [MimiClaw](https://github.com/memovai/mimiclaw) - original inspiration of ESP32 local agent and skills

## 📝 Author

This project is created by [TuyaOpen Team](https://tuyaopen.ai/), with the help of awesome [contributors](https://github.com/tuya/DuckyClaw/graphs/contributors)

[![contributors](https://contrib.rocks/image?repo=tuya/DuckyClaw)](https://github.com/warengonzaga/tinyclaw/graphs/contributors)

---
