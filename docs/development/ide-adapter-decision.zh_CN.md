<p align="right">
  <strong>简体中文</strong> · <a href="ide-adapter-decision.md">English</a>
</p>

# 第一个 IDE 适配器决策 —— Codex

状态：已决策（切片 C · P0-3）。
Owner：Passport Service。

## 决策

**第一个本地 IDE 适配器选 Codex**。Trae 暂缓，直到它给出可从主机侧 Bridge
脚本化调用的、非 GUI 的本地控制面。

本决策只锁定"第一个 IDE 是谁、已经确认的控制面、以及 Slice C 适配器的
验收条件"。它不承诺任何实现代码、传输策略或会话模型。这些由 Slice C 的
后续开发落地。

## 为什么选 Codex，不选 Trae

- **Codex 有可安装的 CLI（`codex-cli` 0.139.0）**：非交互入口有文档、
  自带 stdio 的 MCP server、以及带远程控制的 app-server daemon 实验特性。
  这三条都不依赖 GUI，正好是主机侧 Bridge 需要用来把 Passport 手势转发到
  当前 IDE 会话的能力。
- **Trae 目前只发桌面 App**（`/Applications/Trae.app` 和 `/Applications/Trae CN.app`）。
  它在 macOS 上没有公开的本地 CLI、没有 stdio MCP server、也没有可脚本化
  的 daemon。现在做 Trae 适配器意味着"猜未公开的控制面"，而这直接违反
  仓库规矩。
- **仓库规矩（`ai-guide.zh_CN.md`）**：设备固件和主机 Bridge 不能臆测未
  公开的 IDE 控制 API。Codex 满足，Trae 目前不满足。

Trae 不是被否决，只是暂缓 —— 下文的适配器契约是 IDE 无关的。等 Trae
发布可脚本化控制面（CLI 或有文档的 IPC），同一份契约就能作为第二个
适配器与 Codex 并列。

## 已确认的 Codex 控制面（macOS，本工作机）

用 `codex --version` 和 `codex <cmd> --help` 在开发机上实测过；全部是
Codex CLI 中公开的正式命令：

- **`codex exec [PROMPT]`**：非交互一次性运行。可从参数或 stdin 拿 prompt；
  每次都新起一个进程、跑完就退。适合无状态命令（`review`、单条 utterance），
  不适合承载长时间的 Goal 会话。
- **`codex mcp-server`**：以 stdio 方式把 Codex 启动成 MCP server。这是
  适配器的**首选目标**，因为 MCP 是双向的、长连接的，Bridge Python 已经
  能解析。
- **`codex remote-control start|stop`**：启动带远程控制的 app-server
  daemon，被官方标记 experimental；作为**兜底方案**，只有 `mcp-server`
  覆盖不到必需能力时才启用（比如多会话切换）。

其他已验证存在但**首个适配器不使用**的命令：`resume`、`apply`、`login`、
`mcp`、`sandbox`。它们对 Passport 后续功能有用（会话拾取、审批 apply diff
按钮、MCP 子进程管理），但不属于当前 Goal。

## 适配器契约必须满足

第一个 Codex 适配器至少要满足：

1. 通过 stdio 与 `codex mcp-server` 通信，Bridge 侧 Python subprocess 拉起。
2. 把 Passport Service 协议帧翻译成 Codex 请求：
   - Passport `goal.mode.request` → 打开（或 resume）Codex 会话，回复
     `goal.mode.state=enabled` 并带上 Codex 的 session id。
   - Passport `voice.capture.*` → 作为一次用户轮次投递到当前 Codex 会话。
   - Codex 进度事件 → Passport `task.state` + `task.event`。
   - Codex 审批提示 → Passport `approval.request`；Passport 的
     `approval.decision` → 回给 Codex 的审批响应。
3. 设备端**绝对不存**任何 Codex 凭据。认证只在主机上通过 `codex login`
   完成。
4. 被拒绝的消息用 Bridge 现有的 `> `/`< ` 前缀日志格式打出来，方便 acceptance
   脚本 diff。

## 显式排除，本次不做

- 适配器的具体 Python 模块划分（那属于主机侧 Bridge 重构，不属固件）。
- Codex 响应流形态是分块 stdout 还是 MCP notification —— 由 `mcp-server`
  实测决定，不做臆测。
- 是否把 `codex remote-control` 转成主路径。只在 `mcp-server` 明确覆盖不到
  多会话切换时再评估。
- Trae 适配器。等 Trae 有可脚本化的本地控制面再开。

## 切片 C 适配器验收

只有当真机 + 真实 Codex CLI 满足以下全部条件，切片 C 才算完成：

- 一次 NFC 卡片事件 → Passport 精确发一次 `goal.mode.request`。
- 适配器拉起或 resume 一个 Codex 会话；Passport 在 30 秒内收到
  `goal.mode.state=enabled`，`session_id` 非空。
- 任务进度、任务事件、一次审批往返都能在 Wear 页面上正确显示，全程
  不再需要 mock CLI。
- Acceptance 脚本里的 `mock:` 前缀退场；`!compose`/`!task` 等 mock 命令
  变成可选的开发工具，而不是主路径。

Build、Host tests、Device tests 按仓库规矩分开报告。

## Trae 重新开决策的触发条件

以下**任一条件**成立就重开这个决策：

- Trae 在 macOS 上给出官方支持的 CLI 或 IPC，带有文档化的会话生命周期。
- Trae 暴露一个与 Codex 适配器同 envelope 兼容的 MCP server。
- 有真实产品数据表明用户强需 Trae-first 的 Passport 体验，且团队明确接受
  "写非官方适配器"的成本。

在此之前，Trae 适配器**按规矩暂缓，不是按偏好暂缓**。