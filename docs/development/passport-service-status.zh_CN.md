<p align="right">
  <a href="passport-service-status.md">English</a> · <strong>简体中文</strong>
</p>

# Passport Service 开发状态

状态日期：2026-09-14
分支：`codex/passport-service-mvp`
当前阶段：切片 F Physical Skills MVP 固件已经就位并烧录到设备；真实 NFC、
IDE 适配器和语音 worker 仍在待办。中文页面渲染需要人眼验收。

本文档记录最近一次实测的开发状态，是恢复开发的证据，不是产品发布通告。

## 本阶段完成

- 启动后直接进入 Passport Service 页面用于 MVP 硬件测试。
- 纯 C Service Core 支持有界协议解析、单卡准入、Goal 请求/确认状态、任务
  进度、审批、Skill revision、以及 `task.event`、`tile.stack.state`、
  `context.composed`、`skill.updated`、Wear/Compose 页面状态、事件环形缓冲、
  页面导航和事件确认。
- `passport_ui_model` 产出三个页面（`WEAR.HOME` / `WEAR.TASK` /
  `COMPOSE.STACK`），全部中文文案，顶栏（模式/链路/电量）+ 变长正文行 +
  提示条 + 审批浮层标志分开成型。
- `demo_passport_service` 使用 16 px CJK 子集 LVGL 字体（`ui_cn_16`）
  渲染上述模型。审批浮层是正文区面板，不遮挡顶栏和提示条。
- `tools/passport_bridge.py` 支持 `!compose` / `!event` / `!skill` /
  `!task` / `!approval` / `!help` 手动 mock 命令，无 Tile Reader 也能跑通
  host↔device 流程。
- USB Serial/JTAG 传输沿用 `@passport ` 单行 JSON 帧。
- 真实卡片事件入口保留 `demo_passport_service_nfc_card(const char *card_id)`，
  固件不会自行捏造卡片事件。

## 硬件与烧录证据

- 板卡被识别为 `/dev/cu.usbmodem2101`（ESP32-C3，8 MB Flash）。
- `idf.py build` 产出 `FoloToy-AI-Passport.bin` = 1,557,088 字节。factory
  分区总大小 8,323,072 字节，剩余约 81%。
- `idf.py -p /dev/cu.usbmodem2101 flash` 写入 bootloader（偏移 0x0，
  0x5220 字节）、分区表（0x8000，3,072 字节）和 factory 应用（0x10000，
  1,557,088 字节 / 879,539 字节压缩后）。Hash 校验通过。NVS 未清空。
- 设备通过 RTS 引脚硬复位，主机开始写入后立即观察到 USB Serial/JTAG RX
  日志（`I passport_usb: USB RX bytes=2`）。
- Bridge 成功推送 task.state、task.event、tile.stack.state、
  context.composed、approval.request 等 mock 帧到设备。设备在传输启动时
  就发出 `device.hello`，不再依赖不可靠的 USB Serial/JTAG DTR 位。

## 验证证据

最近一次固件变更后的完整验证：

- `./tools/validate.sh --static`：PASS（197 个文本文件、11 个 host tests）。
- `./tools/validate.sh --firmware`：PASS。
- 固件布局：PASS（factory 分区 0x10000，1,557,088 / 8,323,072 字节）。
- 合并镜像：PASS（Flash 0x0，1,622,624 字节）。
- `idf.py -p /dev/cu.usbmodem2101 flash`：PASS。

设备测试覆盖了烧录、开机、USB Serial/JTAG RX 和主机侧 mock 协议帧的投递。
`WEAR.DISCONNECTED` 断线横幅已在 240×320 实体屏上完成中文渲染视觉验收（
每个字都正常，没有缺字方块），前提是重新生成 `ui_cn_16` 覆盖新加入的
字符串。`tools/acceptance_slice_f.py` 中剩余的 Wear / Compose 检查点仍在
等待人工走一遍。

## 已知边界

- 当前板卡文档没有定义 MCU 侧 NFC Reader API 或引脚。没有外置 Reader 或
  手机中继时，手机模拟卡无法直接通知 ESP32。
- Codex 适配器已决策并落地骨架：`tools/codex_adapter.py`。已在本机实测过
  `codex mcp-server` 握手（Codex CLI 0.139.0）。设备侧接线现在通过
  `passport_bridge.py --codex[/--codex-cwd/--codex-model/...]` 完成：每一条
  设备发出的 `@passport ` 帧都会喂给 `CodexAdapter.handle`，产出的 Passport
  帧走既有 `send_json` 直接回写设备。Host tests：`tests/test_codex_adapter.py`
  (6) + `tests/test_bridge_codex_glue.py` (3)。Slice C 上线前尚待处理：
  (a) 审批往返通道当前用 `bridge.error` stub，等 Codex 审批 notification
  流实测清点；(b) `voice.capture.stop` 需要真实的转录文本，走 Slice D + P0-5。
  真机烟测入口在 `tools/manual_codex_smoke.py`。
- 音频 codec 初始化成功，但 Goal 录音、音频分帧和 Bridge 投递还没有接入
  Passport Service 页面。`WEAR.VOICE` 已在设计中，尚未落码。
- 屏幕已成功初始化，但 Wear/Compose 中文页面仍需人眼在实体屏幕上确认。
- USB Serial/JTAG 传输的活跃度只用协议帧空闲时长（`link_idle_ms`）判断，
  不再看 DTR 位；`passport_transport_usb_connected()` 已删除，代码里也
  不再调用 `usb_serial_jtag_is_connected()`。因为 raw-open 的 Python 桥
  从不 assert DTR，如果仍读它就会把断线横幅永远钉在屏幕上。

## 恢复点

从 [`passport-service-todo.zh_CN.md`](passport-service-todo.zh_CN.md) 起。
接下来最紧要的四件事：(1) 中文页面的实机人眼验收；(2) NFC 输入路径决策；
(3) 第一个 IDE 适配器；(4) 语音 worker。
