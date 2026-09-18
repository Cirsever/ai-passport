[English](/docs/README.md) · **简体中文**

<h1 align="center">AI Passport</h1>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="../assets/images/logo-wordmark-dark.png">
    <img src="../assets/images/logo-wordmark.png" alt="AI Passport 字标" width="180">
  </picture>
</p>

<p align="center">
  <strong>把 IDE 的上下文，带到身边。</strong><br>
  一张卡片选择能力，一块屏幕确认边界，一个实体伙伴持续反馈。
</p>

<p align="center">
  <a href="#它是什么">概念</a> ·
  <a href="#一张卡片如何工作">使用动线</a> ·
  <a href="#当前实现">当前实现</a> ·
  <a href="#开始开发">开始开发</a>
</p>

---

## 它是什么

AI Passport 是一个面向本地 IDE 工作流的实体接口。它不是把聊天窗口缩小到
屏幕上，而是把工作上下文压缩成可以触摸、选择和确认的状态：

- **可写 NFC Skill 卡**：卡片只保存 IDE、模式和 Skill 的有界引用，不保存凭据、
  本机路径、Prompt 或可执行命令。
- **Passport Host Service**：运行在本地 PC，负责识别卡片、解析协议、发现本机
  能力、选择 IDE 会话并编排执行。
- **Passport Device Service Core**：运行在 ESP32-C3 固件中，只显示已经解析的
  状态并上报实体按键，不安装或执行 Skill。
- **像素伙伴**：用卡牌、角色、电池和状态动效表达任务进度、审批边界、断线保留、
  多会话和 PC 端伙伴同步。

卡片是入口，Host Service 是边界，设备是反馈与确认层。任何来源的卡片输入都必须
经过同一套解析和策略检查，不能绕过 Host Service 直接调用 IDE。

<p align="center">
  <img src="../assets/images/passport-ui-v2/01-overview.zh.png" alt="AI Passport 日常陪伴、NFC 扫描、单卡就绪、卡组和任务进展像素界面总览" width="100%">
</p>

<p align="center"><sub>240 × 320 像素界面设计总览：贴卡、唤醒伙伴、读取卡片并确认任务。</sub></p>

## 一张卡片如何工作

当前 V1 卡片记录是一条可以被 NFC Tools 免费版写入的 NDEF Text：

```text
aip:1;i=codex;m=agent;s=review
```

卡片不携带 Skill 实现。Host Service 会把 `i`、`m`、`s` 分别解析为本机已经存在
的 IDE 适配器、运行模式和 Skill；任何字段缺失、重复、超长或不受支持，都会进入
明确的失败状态。

```text
空白 Type 2 卡
    ↓
Passport Host Service 发现本机 IDE 与 Skill
    ↓
配置器生成标准 aip:1 文本
    ↓
Android NFC Tools 免费版手动写卡并读回校验
    ↓
Host Service 严格解析、解析能力并选择会话
    ↓
Passport 显示任务、进度和审批，用户用实体按键确认
```

<p align="center">
  <img src="../assets/images/nfc-fan-demo/nfc-fan-card-concept-v1.png" alt="AI Passport 可写 NFC 卡片的实体概念图，展示卡片、NFC 线圈、磁吸结构和多卡组合" width="100%">
</p>

<p align="center"><sub>实体卡片概念图：卡片负责触发上下文，设备与 Host Service 负责安全地解析和执行。</sub></p>

### V1 和 V2 的边界

| 版本 | 负责什么 | 明确不做什么 |
| --- | --- | --- |
| V1 | 本机已安装 Skill、手动写卡、严格解析、本机测试和 IDE 调度 | 不自动安装远程 Skill，不把完整 Prompt 或命令写进卡片 |
| V2 方向 | 使用不可变 GitHub revision 的 manifest，按信任策略暂存、校验并注册 Skill | 不因一张不可信卡片静默下载、执行或获得凭据 |

完整协议和安全边界见[可写 NFC Skill 卡设计](development/nfc-skill-card-design.zh_CN.md)。

## 设备上看到什么

Passport 的界面不是日志面板，而是一个有优先级的实体状态层：

| 场景 | 设备反馈 |
| --- | --- |
| 贴卡 | 扫描线、单卡停留和多卡错位叠放；解析失败显示“卡片协议错误” |
| 任务 | 伙伴、任务标题、进度条和当前会话；断线时保留上次可信状态 |
| 审批 | 操作卡显示动作、范围和来源；详情页分页展示完整路径或命令 |
| 会话 | 长按上键打开选择器；主机确认前显示“正在切换”，不把迟到回执当成成功 |
| 录音 | 固定开始时的 Bridge、session 和 epoch；松开确认键立即停止，不改投其他会话 |
| 伙伴 | PC 端更换伙伴后，由 Host Service 在空闲通信周期自动分块同步，设备无手动换宠入口 |
| 电量 | 四格像素电池；未知状态显示问号，不把读取失败伪装成 0% |

<p align="center">
  <img src="../assets/images/passport-ui-v2/03-overview.zh.png" alt="AI Passport 多会话选择、会话切换和 PC 伙伴自动同步像素界面总览" width="100%">
</p>

<p align="center"><sub>多会话与伙伴同步：任务、语音和审批始终绑定明确的 IDE 会话。</sub></p>

<p align="center">
  <img src="../assets/images/passport-ui-v2/05-invalid-card.zh.png" alt="AI Passport 非法 NFC 卡提示像素界面" width="320">
</p>

<p align="center"><sub>非法卡不会启动 IDE，也不会改变当前会话。</sub></p>

## 当前实现

当前分支已经包含 Passport Service v2 的主链路：

- Codex app-server 的精确 thread/session 路由与三条会话目录；
- 带详情页、逐请求决定和回执状态的审批流程；
- 240 × 320 像素 UI、图形电池、卡组和断线保留；
- 录音 route snapshot，防止会话切换时 PCM 投递到错误对话；
- PC 端伙伴读取、32 × 32 量化、SHA-256 校验和空闲分块同步；
- Trae 的只读降级路径和 protocol 1 兼容路径；
- 可写 NFC Skill 卡的 Host Service 设计基线，当前仍以 V1 手工写卡验证为边界。

<p align="center">
  <img src="../assets/images/passport-ui-v2/pet-sync-flow.zh.png" alt="AI Passport PC 伙伴变化被 Host Service 检测、分块传输并原子替换的流程图" width="100%">
</p>

设计状态、协议字段和已知未验收项目见：

- [Passport Service 当前状态](development/passport-service-status.zh_CN.md)
- [Passport v2 协议](development/passport-v2-protocol.zh_CN.md)
- [Passport 像素伙伴界面设计](development/passport-pixel-ui-design.zh_CN.md)
- [可写 NFC Skill 卡设计](development/nfc-skill-card-design.zh_CN.md)

## 开始开发

### 先读边界

1. 阅读 [`AGENTS.zh_CN.md`](../AGENTS.zh_CN.md) 和[AI 开发指南](development/ai-guide.zh_CN.md)。
2. 检查并安装仓库要求的五个技能：`passport-develop`、`passport-setup`、
   `passport-build`、`passport-device-test`、`passport-debug`。
3. 需要硬件事实时，只以[硬件开发指南](hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md)
   和 [`components/bsp/include/bsp_pins.h`](../components/bsp/include/bsp_pins.h) 为准。
4. 应用页面、状态机和动画放在 `main`；可复用板级逻辑放在 `components/bsp`。
5. 二次开发必须重新设计应用 UI，不能把当前硬件测试菜单当成成品应用。

### 用一句需求开始

可以从这段需求开始，再替换成你的目标：

```text
为 AI Passport 开发一个离线习惯打卡应用。
使用 240 × 320 屏幕和三个实体按键，记录需要跨重启保留。
从 main 创建 feature/* 分支，先阅读 AGENTS.md 和相关硬件指南。
保持硬件逻辑在 components/bsp、应用逻辑在 main。
重新设计页面和交互，不复用当前硬件测试菜单。
完成主机测试和固件构建，并分别报告 Build、Host tests、Device tests、
以及尚未验证的真机项目。
```

### 构建与验证

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
./tools/validate.sh
```

验证通过不等于真机验收。烧录前必须运行 `./tools/validate.sh --preflash`，
并确认目标设备、固件和存储影响后再进行物理烧录。

## 硬件基线

**ESP32-C3 · 8 MB Flash · 无 PSRAM · 240 × 320 RGB565 · 三个实体按键**

默认分区只有 NVS、PHY data 和一个占用剩余空间的 factory 应用。显示、按键、音频、
电池、共享 I2C、Wi-Fi 扫描和 BLE 广播的边界，以 [`bsp_pins.h`](../components/bsp/include/bsp_pins.h)
和[硬件开发指南](hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md)为准。

## 文档入口

| 主题 | 文档 |
| --- | --- |
| 开发规则与技能 | [开发文档索引](development/README.zh_CN.md) · [AI 技能](../skills/README.zh_CN.md) |
| 协议与主机 | [Passport v2 协议](development/passport-v2-protocol.zh_CN.md) · [NFC Skill 卡](development/nfc-skill-card-design.zh_CN.md) |
| UI 与资产 | [像素 UI 设计](development/passport-pixel-ui-design.zh_CN.md) · [资产说明](../assets/README.zh_CN.md) |
| 硬件与构建 | [硬件指南](hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md) · [构建与测试](development/engineering/build-and-test.zh_CN.md) |
| 贡献与许可 | [贡献指南](../.github/CONTRIBUTING.zh_CN.md) · [MIT License](../LICENSE) |

---

AI Passport 的目标不是把电脑上的窗口搬到设备上，而是让上下文、边界和决定在
实体世界里变得清楚、可触摸、可验证。
