<p align="right">
  <a href="passport-service-todo.md">English</a> · <strong>简体中文</strong>
</p>

# Passport Service TODO

这是 Passport Service MVP 的唯一后续开发清单。已完成事项保留在这里，方便下一次
开发准确区分“已经验证”和“计划实现”。

当前阶段：**Passport 固件与服务基础能力完成 · Slice C/D/F 关键路径落地**
最近证据：[`passport-service-status.zh_CN.md`](passport-service-status.zh_CN.md)

## P0——端到端 MVP 前必须完成

- [x] 启动后直接进入 Passport Service 页面。
- [x] 完成纯 C Service Core 和 Host tests。
- [x] 完成 USB Serial/JTAG 分帧和标准库 Bridge。
- [x] 移除自动模拟卡、模拟任务和自动 Goal 行为。
- [x] 增加真实卡片事件入口和单卡准入规则。
- [x] 展示主机链路、电量、任务、NFC、Goal、审批和 Skill 信息。
- [x] 在已连接的 Passport 上完成当前固件的构建、烧录和启动验证。
- [x] 选定 NFC 输入路径 —— 先做手机→Bridge 中继；外置 NFC Reader 保留为
  后续任务。决策记录见 [`nfc-path-decision.zh_CN.md`](nfc-path-decision.zh_CN.md)。
  - [x] Bridge HTTP 中继端点已接通 `tools/passport_bridge.py`
    （`--nfc-relay-port` / `--nfc-relay-host`）；Host tests 在
    `tests/test_nfc_relay.py`。三种手机侧方案见决策文档。
  - [ ] 后续：外置 NFC Reader 接入 Passport，并记录文档或实测确认的型号、
    总线、引脚和功耗。只在决策文档列出的触发条件下重开。
- [ ] 用真实手机/卡片完成一次端到端测试。**需要操作员**：配合选定的中继
  形态，一次真实贴卡应恰好触发一条 `goal.mode.request`，第二张 UID 被
  wire 上拒绝。
- [x] 选择第一个本地 IDE 适配器：Codex（决策记录在
  [`ide-adapter-decision.zh_CN.md`](ide-adapter-decision.zh_CN.md)）。Trae 暂缓，
  直到它给出可脚本化的本地控制面。
- [x] 实现 `goal.mode.state`、任务进度和会话身份的适配器契约：
  `tools/codex_adapter.py` 通过 MCP `tools/call codex` + `codex-reply`
  完成，Host tests 在 `tests/test_codex_adapter.py`。审批往返和实机端到端
  接线是后续工作，见 `passport-service-status.zh_CN.md`。
- [x] 把适配器接入实机链路：`passport_bridge.py --codex[/--codex-cwd/
  --codex-model/...]`，设备侧每条 `@passport ` 帧走 `CodexAdapter.handle`，
  产出的帧走既有 `send_json`。胶水层测试
  `tests/test_bridge_codex_glue.py`。
- [x] Goal 模式语音 stub：设备侧 OK 长按发一对
  `voice.capture.start` / `voice.capture.stop`，Codex 适配器把它当一次
  utterance 处理。真实音频 worker + STT 保留在切片 D + P0-5
  （见 `physical-skills-mvp-design.zh_CN.md`）。

## P1——硬件和交互验证

- [ ] 在实体 240×320 屏幕上目视检查烧录页面：无裁切、状态可读、页面可切换、
  审批提示正确。**需要操作员** —— 使用 `tools/acceptance_slice_f.py`。
- [ ] 在真实主机连接下操作 `UP`、`DOWN`、`OK`，确认每个动作只发送一次。
  **需要操作员**。
- [ ] 由 Bridge 发送真实的 `task.state`、`goal.mode.state` 和 `approval.request`，
  验证设备状态迁移。**需要操作员** —— 离线部分由 C 集成测试
  `tests/test_passport_service_integration.c` 覆盖，实机部分仍由人工核对。
- [x] TCP/SoftAP 备用链路决策完成：代码保留、默认不启用、不动 PC Wi-Fi。
  见 [`softap-fallback-decision.zh_CN.md`](softap-fallback-decision.zh_CN.md)。
- [x] 覆盖卡片准入、Goal 确认、任务进度、审批、事件确认、Skill reload、
  断线时序的集成测试：`tests/test_passport_service_integration.c`。

## P2——加固与交付

- [x] 被拒绝的主机消息给出有界错误反馈：`demo_passport_service.c` 现在会
  在 Service Core 拒绝时发出一条 `protocol.reject` 帧，附截断后的原始
  行内容。
- [ ] 记录最终 NFC Reader/手机中继的接线和运行功耗。**需要操作员** —— 等
  真实中继方案接入手机后填。
- [ ] 记录最终 IDE 适配器的兼容范围和失败恢复方式。**需要操作员** —— 等
  真实 Codex 会话通过 bridge 在设备上跑一次后填。
- [x] 每次修改传输或音频后执行完整验证门禁：由结构强制 —— `./tools/validate.sh`
  是唯一入口，同时跑 static + firmware；开发者可以按需只跑 `--static` /
  `--firmware`。
- [x] 每次交付都分开报告设备测试结果和构建结果：`AGENTS.md` 里已经规范化
  四段格式（Build / Host tests / Device tests / Unverified），
  `passport-service-status.zh_CN.md` 里也遵循同一规矩。

## 切片 F —— Physical Skills MVP

设计文档：[`physical-skills-mvp-design.zh_CN.md`](physical-skills-mvp-design.zh_CN.md)。

- [x] 扩展 `passport_service`：解析 `tile.stack.state`、`context.composed`、
  `task.event`、`skill.updated`；新增页面/堆叠/事件环形缓冲。
- [x] 重构 `passport_ui_model`：随身首页 / 随身任务 / 组合堆叠三页面，
  使用中文文案，独立提供 header / body_rows / hint 字段。Host tests
  锁定新契约。
- [x] 重构 `demo_passport_service`：顶栏 + 正文行 + 提示条框架，审批浮层
  隐藏在正文之上；导航和事件确认走新的
  `passport_service_navigate` / `passport_service_ack_top_event`。
- [x] 在 `tools/passport_bridge.py` 中加入
  `!compose` / `!event` / `!skill` / `!task` / `!approval` 手动 mock 命令。
- [x] 打包一份 LVGL CJK 子集字体（约 40–60 个字形）覆盖 `passport_ui_model`
  产出的所有中文字符，并把 `demo_passport_service` 切换为该字体。
  再生流程见 [`main/fonts/README.zh_CN.md`](../../main/fonts/README.zh_CN.md)
  （`tools/gen_cjk_font.sh` + `tools/collect_ui_glyphs.py`）。
- [ ] 实机验收：烧录后用 mock CLI 完整走一遍 随身首页 ↔ 随身任务 ↔
  组合堆叠，确认审批浮层、事件日志、组合选择都能正确显示中文，
  没有缺字。**需要操作员** —— 脚本 `tools/acceptance_slice_f.py`。
- [ ] 语音 worker（`voice.capture.*`）与真实 `WEAR.VOICE` 浮层。当前 stub
  已经上线（见前面 `send_voice_capture_burst`），真实音频 worker 仍属
  切片 D。
- [x] `WEAR.APPROVAL` 60 秒超时和 `WEAR.DISCONNECTED` 过期横幅：
  `passport_service_tick()` 对挂起审批和链路空闲各自计时，Demo 层在链路
  空闲超过 3 秒时展示整屏"与主机失联 · 过期起自 xx 秒前" + 提示条
  `上 重试  下 快照`。

## 恢复开发顺序

1. 操作员完成中文页面视觉验收（`tools/acceptance_slice_f.py`）。
2. 用真实手机跑一次 NFC 中继端到端（选一个中继方案）。
3. 用 bridge `--codex` 跑一次真实 Codex 会话，抓 approval notification 流
   并替换 `tools/codex_adapter.py::_forward_approval_decision` 里的 stub。
4. 切片 D 音频 worker（真实 `voice.capture.*` 载荷 + `WEAR.VOICE` 浮层）。
5. 把实测结果更新回本清单和
   [`passport-service-status.zh_CN.md`](passport-service-status.zh_CN.md)。
