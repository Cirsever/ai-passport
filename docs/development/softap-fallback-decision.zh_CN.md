<p align="right">
  <strong>简体中文</strong> · <a href="softap-fallback-decision.md">English</a>
</p>

# SoftAP / TCP 备用链路决策 —— 保留代码，不默认启用

状态：已决策。
Owner：Passport Service。

## 决策

**保留** SoftAP + TCP 传输路径的代码，**不默认启用**，**不要求开发者
调整 PC Wi-Fi 状态**。USB Serial/JTAG 是 MVP 交付阶段唯一的默认传输。

本决策不改动 PC Wi-Fi 配置，与
`docs/development/passport-service-status.zh_CN.md` 的既有规矩一致。

## 保留的理由

- **零回归成本。** `main/passport_transport_tcp.c` 已存在、可编译、不占用
  USB 路径需要的引脚。它由可选 demo 入口触发，不属于 Passport Service
  默认页，保留在树内不影响默认启动路径。
- **无线演示保险。** 存在一种 USB 不可用的合理场景：现场演示时只有
  电池 + 无线 Passport + 一部跑 Bridge 的手机。TCP + 临时 SoftAP 能覆盖，
  不需要新模块。
- **对 DidTiboRest 的备位。** Tibo 推送想法一直把 BLE/Wi-Fi 也纳入候选
  传输。今天删掉 TCP 骨架，未来要求无线演示时会立即回锅讨论。

## 不默认启用的理由

- **DTR 不可靠是 USB 的问题，不是 TCP 的问题** —— 但在 USB 旁再加一条
  TCP 备用链路只是把"链路 liveness"话题多复制一份，验收规则
  （`link_idle_ms > 30 s`）依然适用。两条运行方式意味着操作员多一处
  "未知状态"入口。
- **SoftAP 会碰我们明文承诺不动的 NVS 分区。** 一旦 Passport 开始广播，
  设备或主机缓存的 Wi-Fi 凭据就成了验收面的一部分。USB 保持 NVS 静默。
- **Passport-first 的用户体验** 意味着手机是操作员设备（见 NFC 中继
  决策）。如果手机通过 HTTP 跟 Mac 说话，再加一条 SoftAP 让手机也直接
  跟 Passport 说话，是重复通道，不是升级。

## 只有以下触发条件成立才重开

- 操作员明确要求无线演示，或者现场连接方式强制无线。
- USB Serial/JTAG 在比当前开发板更广的硬件面上表现不稳定。
- 未来某个切片需要一条 USB 承载不了的 Passport ↔ 手机长期数据通道。

在此之前，SoftAP demo 保留在树里但不启用；Passport Service 传输门禁
只认 USB Serial/JTAG。

## 对 `--codex` 和 `--nfc-relay` 的影响

`tools/passport_bridge.py` 的两个 host 侧 flag 都从 Passport 线协议
读写，与底层传输无关。以后接 SoftAP 时，两条路径都无需改动 —— 它们
挂在同一个进程内 `send_json` sink 上，底层是 TCP socket 还是 USB serial
fd 都一样。

## 验收

- 本决策不改任何固件代码路径。
- `tools/passport_bridge.py` 保持 `--usb` 为默认，TCP 通过 `--host/--port`
  显式启用。
- `docs/development/passport-service-todo.zh_CN.md` 把 TCP/SoftAP 决策
  条目标记完成；未来重开是一项新任务，不是二次辩论。