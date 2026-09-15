<p align="right">
  <strong>简体中文</strong> · <a href="nfc-path-decision.md">English</a>
</p>

# NFC 输入路径决策 —— 先做手机→Bridge 中继

状态：已决策（切片 D · P0-1）。
Owner：Passport Service。

## 决策

第一版 Passport NFC 入口选 **手机→Bridge 中继**。外置 PN532/MFRC522 Reader
接到 ESP32-C3 的方案继续保留在文档里，但不作为 Slice C 端到端 MVP 的
阻塞项。

两条方案的固件侧终点是同一个入口：
`demo_passport_service_nfc_card(const char *card_id)`。以后切换外置 Reader
只是主机侧改动，不需要重构固件。

## 现在选中继的原因

- **硬件零改动。** 当前基线板已经贴了一枚被动 NTAG213，任何现代手机
  都能读。不动 BSP、不加总线、不做电源预算、不重打板。
- **复用既有入口。** Service Core 已经通过
  `demo_passport_service_nfc_card(card_id)` 暴露卡片入口，并在
  `passport-service-architecture.zh_CN.md §5` 中确认"第一张卡准入后拒绝
  第二张"。中继要做的只是把 UID 送进这个入口，语义上与
  `tools/passport_bridge.py` 现有的 mock CLI 完全一致。
- **契合 MVP 的问题域。** MVP 要验证的是 Passport → Bridge → Codex 环路，
  不是 RF Reader 的工业可靠性。把硬件工作后置，Slice C 才能保持无阻塞
  推进。
- **回切外置 Reader 只是一次改动。** 外置 Reader 到货后，作为 BSP 外设
  发出同样的规范化 `card_id` 字符串；两条路径在 Service Core 汇合。

## 现在为什么不上外置 Reader

- **新驱动债务。** ESP-IDF 5.5.3 没有官方 PN532/MFRC522 驱动，我们要引
  第三方组件、在自己板上验证、然后长期维护它。跟 MVP "先验证再落盘"的
  节奏不合。
- **引脚吃紧。** `components/bsp/include/bsp_pins.h` 已占用
  GPIO 0/1/2/3/4/5/6/7/8/9/10/20/21，18/19 保留给 USB Serial/JTAG。
  剩下 11/12/13 加一个 IRQ 线勉强够用，但每加一根都要走一次全队 BSP
  审计。
- **功耗预算问题。** PN532 读操作瞬时约 100–150 mA @ 3.3 V。CW2017
  和当前 LEDC 能扛，但这份分析属于独立工作，今天做没有额外产出。
- **机械不确定性。** Passport 外壳没有明文标注的 NFC 天线窗口。没有
  实测 RF 表征就不能承诺读距。
  [physical-skills MVP 设计稿](physical-skills-mvp-design.zh_CN.md#12-在切片路线中的位置)
  已经说明 Phase 0 RF 表征必须先于任何内嵌 Reader。

## 中继契约

固件之外的一切都归主机。以下约束是"以后回切外置 Reader 时保持迁移成本
可控"的最小接口，实现必须遵守：

- **与未来的 Reader 使用同一份规范化。** 主机发出的 `card_id` 字符串必须
  满足 `demo_passport_service_nfc_card()` 里既有的规范化规则（字母、
  数字、`-_:.`，长度 < `PASSPORT_SERVICE_ID_MAX`）。任何不满足的输入在
  主机侧就拒，不进 `@passport goal.mode.request` 帧。
- **一个 Passport 会话一张卡。** Service Core 已经拒第二张卡，中继不能
  用重试掩盖；如果第二部手机贴上来，把它显式给操作员，而不是重发。
- **设备端不存凭据。** 手机侧向本地 Bridge 走 loopback 或操作员级 token
  认证；ESP32 只见到规范化 `card_id`。
- **去抖动放在主机。** 同一部手机短时间多次贴卡必须合并成一个 Passport
  帧。
- **失败要显式。** 中继掉线是 wire 上的一条 `bridge.error`，不是伪造
  成功。与 Codex 适配器 approval stub 的约定一致。

## 中继的具体选项（不属决策）

决策只锁类别，不锁具体工具链。以下任意一个都能实现上面契约，选哪个
留给切片 D 的后续任务：

- **iOS Shortcut → 本地 HTTP。** 原生 NFC 读，Shortcut 触发后 POST
  `card_id` 到开发机上的 Bridge endpoint。用户端零安装，做原型最快。
- **Android Web NFC（Chrome）。** 读取 NTAG213，用一个自托管小页面
  POST 到 `http://<mac>:PORT`。零安装，Android 12+ + Chrome 就能跑。
- **小型原生 Android app。** 成本略高，但当演示环境没有 Wi-Fi 局网时
  比较可靠。

## 显式延后的工作

- 选一个具体中继方案，接通 Bridge 的 HTTP endpoint，在
  `tools/passport_bridge.py` 里加一个 `--nfc-relay-port` flag，复用现有
  事件循环（跟 `--codex` 同一套 select）。
- 在 `docs/development/passport-service-status.zh_CN.md` 里记录中继握手
  形态（帧格式、重试策略、TLS 还是 loopback）。
- 以下**任一条件**成立就重开外置 Reader 决策：
  - 用户抱怨"手机绕一层"体验不 wearable。
  - Passport 硬件出现有文档的 NFC 天线窗口，直贴演示的可信度变得
    必要。
  - 有固定桌面底座，直接接线的 Reader 更省事。

在这之前，外置 Reader **按成本/收益暂缓，不按原则否决**。

## 首个中继实现的验收标准

- 一次真实的手机贴卡（NTAG213）恰好触发一条 `goal.mode.request` 帧
  抵达设备。
- 换一部手机贴新 UID，wire 上出现 `bridge.error`，**不产生**第二条
  `goal.mode.request`。
- 中继退出时若已经准入一张卡，设备侧断线阈值（`link_idle_ms`）仍按
  既有规则显示横幅，不能悄悄丢卡。
- 固件侧零改动，所有变化仅限 `tools/`。