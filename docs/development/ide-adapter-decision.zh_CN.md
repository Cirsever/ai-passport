<p align="right">
  <strong>简体中文</strong> · <a href="ide-adapter-decision.md">English</a>
</p>

# 第一个 IDE 适配器决策 —— Codex（Trae 侧的一次性 chat 拉起已上线）

状态：已决策（切片 C · P0-3）；Trae 适配器 2026-09-15 补充落地，仅覆盖
fire-and-forget `trae-cn chat`，不承诺双向能力。
Owner：Passport Service。

## 决策

**第一个本地 IDE 适配器仍是 Codex**，因为只有它给出完整的 stdio MCP
双向流、`elicitation/create` 审批往返、以及 `tools/call codex-reply` 的
多轮承接。**Trae 适配器现在也已可选启用**，但意义有限：它只覆盖"贴卡
→ 拉起 Trae Chat 窗口 → 把 utterance 追加到当前会话"这条最小闭环。
task 事件、审批往返、会话 id 反馈都由适配器合成，Trae 不真报告。

本决策锁定"第一个 IDE 是谁、已经确认的控制面、以及 Slice C 适配器的
验收条件"。它不承诺任何实现代码之外的传输策略。这些由 Slice C 的后续
开发继续演进。

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

第一条**已经成立了一半**：Trae CN 3.3.98 的
`/Applications/Trae CN.app/Contents/Resources/app/bin/trae-cn chat <prompt>`
是文档化的官方 CLI，可以从 Bridge 脚本化拉起。因此本次落地了一份最小的
`tools/trae_adapter.py`（见下节"Trae 适配器 · fire-and-forget"），
但它不能取代 Codex 适配器 —— Trae 目前仍没有：

- stdio 层的机器可读回执（只有窗口渲染副作用）；
- 会话 id 反馈（`trae_adapter.py` 用 uuid 合成一个，供 Passport UI 显示）；
- approval 通道回调（`approval.decision` 走 `bridge.error`）。

在这三条都落地之前，Trae 适配器仅用于"Passport 卡片直连 Trae Chat 窗口"
的最小演示；生产链路仍以 Codex 适配器为准。

## Trae 适配器 · fire-and-forget（2026-09-15 落地）

**范围**（仅这些）：

- Passport `goal.mode.request` → `trae-cn chat -m agent "..."` 一次调用，
  合成 `goal.mode.state=enabled` + `task.state` 帧回设备；session_id 是
  `trae-<uuid8>` 合成串。
- Passport `voice.capture.stop` 且带 `text` → 再一次 `trae-cn chat` 把
  utterance 送进同一 Trae 窗口，合成一条 `task.event`「已投递到 Trae Chat」
  给设备。
- Passport `approval.decision` → 显式 `bridge.error`，因为 Trae 没有
  scriptable approval endpoint；操作员必须直接在 Trae 窗口里点。

**不做**：会话保持（每次 `trae-cn chat` 都是新进程）、任务进度回推、审批
往返、`voice.capture.audio` 真实音频路径、多会话切换。

**wire 命令**：

```bash
python3 -u tools/passport_bridge.py --usb --serial /dev/cu.usbmodemXXX \
    --trae [--trae-cwd /path/to/project] [--trae-mode agent|ask|edit]
```

`--codex` 与 `--trae` 互斥。`--trae-binary` 缺省时自动探测
`/Applications/Trae CN.app` 与 `/Applications/Trae.app`。

**主机侧测试**：`tests/test_trae_adapter.py`（11 项，含
子进程 stub、二 IDE 拒收、UTF-8 CJK utterance、approval 明确不支持、
可见性提示）+ `tests/test_bridge_trae_glue.py`（5 项，桥胶水层，含 NFC
中继 pipeline 路由）。

**首次实战复盘（2026-09-16）**：

- 首次 `--trae` 实战看起来失败：操作员看不到新的 Trae Chat 窗口。窗口
  其实开了 —— `-r`（reuse-window）是 CLI 默认值，prompt 就落进了当前
  聚焦的 Trae 窗口里，跟正在进行的对话混在一起。修复：adapter 现在用
  `-n --maximize` 强制每次开独立窗口。workspaceStorage 侧信道旁证：
  每次调用都会新增一个 `workspaceStorage/...` 目录。
- 即便有 `-n`，新窗口也可能被派生它的窗口叠住。adapter 现在每次 chat
  成功后向 stderr 输出一行提示，告诉操作员用 Mission Control（F3）或
  ⌘\` 把新窗口切到前台。
- 同一次实战里 NFC 中继路径也失败了。中继把 `goal.mode.request` 直接
  丢到 wire 上，但这个方向在协议里是"设备→主机"—— 设备侧行解析器
  拒收。修复：`_drain_nfc_outbox` 现在在 pipeline（Codex 或 Trae）
  存在时把中继帧分发到 pipeline 而不是 wire，只有 pipeline 合成的
  `goal.mode.state`、`task.state` 才发到设备。
- 固件侧同步做了一次单卡准入放宽：`parse_goal_mode_state` 原本要求
  帧里的 card_id 已经在 `state->goal_card_id` 里注册过（只有真实
  NFC reader 触发才会写入），中继路径没有 reader → 永远拒收。现在
  `state->goal_card_id` 为空时，wire 上第一帧 `goal.mode.state`
  的 card_id 就被采纳；第二张不同 card_id 的帧仍然拒收 ——"一张卡、
  不换卡"契约保留。