<p align="right">
  <a href="passport-service-architecture.md">English</a> · <strong>简体中文</strong>
</p>

# Passport 服务架构

## 1. 产品边界

Passport 是运行在 Mac 或 PC 上的本地 Agent 伴侣，不执行 Codex、Trae、工具、
权限或 Memory。设备负责物理输入、音频采集、显示、有界任务状态和到本地
Bridge 的传输；Bridge 负责 IDE 选择、Goal 模式开启、语音投递以及不同 IDE 的
适配器。

目标动线是：

```text
手机模拟一张 NFC 卡
        │
        ├─ Passport 外接 NFC Reader，或明确的手机到 Bridge 中继
        ▼
Passport 收到卡片事件
        ▼
本地 Bridge 选择 Trae/Codex 适配器并开启 Goal 模式
        ▲                                      │
Passport 按键启动本地录音 ─────────────────────┘
        │
        ▼
Bridge 把语音交给当前 IDE 会话
        │
        ▼
IDE 进度 / 审批 / 完成状态 → Passport 屏幕
```

当前板卡文档是一个必须先解决的硬件门禁：公开的 NFC 器件是被动 NTAG213，
ESP32 没有文档定义的 MCU 侧 NFC Reader API 或引脚分配。手机可以读写这个
被动标签，但手机模拟标签不会通过它主动通知 ESP32。因此，直贴动线必须增加
外置 Reader，或者改成手机中继方案。

## 2. 当前 MVP 范围

- 通过 USB Serial/JTAG 和 `@passport ` 行封装维护一个主机连接；保留 SoftAP/TCP
  作为开发备用通道。
- 接收本地 Bridge 发来的有界任务、审批、Goal 会话和 Skill revision 更新。
- 显示任务进度、审批文本、卡片身份和 Goal 状态。
- 由实体按键发出审批决定。
- 通过 `passport_service_load_goal_card()` 接受每个 Service 会话中的一张规范化卡片。
- 为被接受的卡片发送一次 `goal.mode.request`，等待 Bridge 确认当前 IDE 会话。
- 固件不包含 Codex/Trae 专属快捷键，也不保存凭据。

当前固件不会自动加载卡片、注入离线任务进度或自动确认 Goal 模式。Host Bridge
现在只是协议客户端；这些事件必须由真实 IDE 适配器提供。

## 3. 系统边界

```text
Mac / PC
  Trae 适配器       Codex 适配器       任务/进度来源
       \                |                /
                  Passport Bridge
       │ USB Serial/JTAG / BLE / TCP
       ▼
Passport Service Core
  有界协议状态
  单卡准入
  Goal 请求/确认状态
  审批动作
       ├── LVGL UI
       ├── 按键事件适配器
       ├── 音频 worker
       └── NFC 事件适配器
             ├── 外置 NFC Reader（直贴必须）
             └── 手机中继事件（替代方案）
```

纯 C Service Core 不依赖 LVGL、NimBLE、socket 或 NFC。设备适配器把真实卡片
事件转换成 Core 调用，消费产生的 action，再通过当前传输发送请求。

参考语音实现使用 BLE notification 发送压缩后的 16 kHz 音频和原始按键手势，
由桌面 Bridge 决定映射成具体应用快捷键。本项目也沿用这个分工：Passport 只
报告手势和音频，Bridge 决定当前目标是 Trae 还是 Codex。

## 4. 服务协议

协议 v2 的会话路由、审批回执和自动伙伴资源传输见
[v2 通信契约](passport-v2-protocol.zh_CN.md)。下列 protocol-1 消息继续作为
单会话兼容路径。

USB 使用每行一个 UTF-8 JSON 对象，并带 `@passport ` 前缀。没有此前缀的启动日志
由 Bridge 忽略。示例：

主机发送给 Passport：

NFC 中继在启动 IDE 之前发送 `{"type":"nfc.present","card_id":"card-1"}`，
让屏幕立即播放动画。`card_id` 必须符合 `[A-Za-z0-9_:.-]{1,47}`。
该观察事件不授权 Goal 会话、不绑定会话身份，也不生成动作。重复观察不会重播动画。
多卡布局只来自 `tile.stack.state`；就绪状态来自 `context.composed` /
`goal.mode.state`，不能由动画计时器决定。

```json
{"type":"task.state","task_id":"runtime-1","state":"running","progress":42,"summary":"Refactoring tracing"}
{"type":"goal.mode.state","mode":"goal","state":"enabled","card_id":"card-1","ide":"codex","session_id":"codex-session-1"}
{"type":"approval.request","request_id":"request-7","summary":"Apply 3 files changed"}
```

Passport 发送给主机：

```json
{"type":"device.hello","protocol":1,"device":"FoloPassport"}
{"type":"goal.mode.request","mode":"goal","card_id":"card-1"}
{"type":"approval.decision","request_id":"request-7","decision":"approve"}
```

Skill revision（主机→Passport）沿用同一 envelope：

```json
{"type":"skill.revision","skill_id":"codex.review","revision":"r7","summary":"Review firmware diff"}
```

DidTiboRest 切片的通知消息扩展同一命名空间，定义见
[`did-tibo-rest-idea.zh_CN.md`](did-tibo-rest-idea.zh_CN.md)：
`notify.push`、`notify.mute`、`notify.level`、`notify.state`。任何后续切片新增
`type` 名称必须先落到本文档，两个切片共用同一条 envelope。

协议规则：

- 未知或格式错误的消息被拒绝，不能修改有效状态。
- 字符串复制到设备内存前必须限长。
- 只有 Bridge/IDE 适配器可以确认当前 Goal 会话。
- 第一张卡激活后，第二个卡片身份必须拒绝。
- 设备不接收也不保存 IDE 凭据。

### 4.1 传输抽象

Service Core 只依赖一个窄化的行传输接口，不直接依赖 USB、BLE 或 TCP：

```c
typedef struct {
    int  (*send_line)(const char *utf8_line);
    void (*on_line)(const char *utf8_line, void *ctx);
} passport_transport_t;
```

当前实现是 USB Serial/JTAG。切换到 BLE notification 或 TCP 不能改动 Service Core；
切片 A 的验收覆盖一个 mock 传输，跑同一套主机测试矩阵。

### 4.2 超时与回退

- `goal.mode.request` 等 `goal.mode.state=enabled` 最长 30 秒。超时后 Passport
  回到 `NO_CARD`，本地页面显示 `goal.mode.state=timeout`，需要新的卡片事件才能
  重试。
- `approval.request` 60 秒内没有匹配的 `approval.decision` 时，按键提示恢复到
  之前的状态，但审批横幅保留，直到 Bridge 取消或解决。
- 断线重连时，只有当 Bridge 主动重放 `goal.mode.state=enabled`，设备才保留上一次
  接受的卡片；否则回到 `NO_CARD`。

## 5. 设备状态模型

```text
NO_CARD
  └─ CARD_REQUESTED
       └─ GOAL_ENABLED
            ├─ IDLE
            ├─ RUNNING
            ├─ WAITING_APPROVAL
            ├─ DONE
            └─ ERROR
```

卡片事件是输入边界，不是定时器模拟：

```text
真实 Reader / 手机中继
        → passport_service_load_goal_card(card_id)
        → goal.mode.request
        → Bridge 选择并启动 IDE 适配器
        → goal.mode.state=enabled
```

按键和音频遵循同样的主机归属原则：

```text
按键手势 → 设备立即启动采集
          → 音频/事件传输 → Bridge
          → 当前 IDE 适配器
```

## 6. 固件规则

- 按键回调必须非阻塞；音频采集放在 worker task。
- 所有 LVGL 访问都必须持有 `bsp_lvgl_lock()`。
- 传输适配器不能自行发明卡片、任务、审批或 IDE 状态。
- USB 是当前实验室传输；协议边界稳定后，BLE 是无线音频/事件传输的目标方案。
- NFC Reader 代码必须放在 BSP/事件适配器，并且只能使用硬件文档或实测确认的
  引脚和总线。

## 7. Passport 页面信息布局

页面只展示“设备当前能确认的事实”，不把手机卡片、IDE 选择或网络连接状态混在
一起。当前屏幕按从上到下分成四块：

```text
┌────────────────────────────┐
│ HOST WAIT / USB READY  BAT │  主机链路和电量
├────────────────────────────┤
│ RUNNING 42%                │  当前任务状态和进度
│ Refactoring tracing        │  一行任务摘要
├────────────────────────────┤
│ NOTIFY: Tibo push (2)      │  可选事件行，空时折叠
├────────────────────────────┤
│ NFC: card-1                │  最近接受的卡片身份
│ GOAL: CODEX / session-1   │  Goal 状态、IDE 和会话
├────────────────────────────┤
│ GOAL ACTIVE  DOWN info    │  当前可用的按键提示
└────────────────────────────┘
```

状态展示规则：

- 没有卡片时显示 `NFC: NO CARD` 和 `Tap NFC card to start`，不能发送 Goal 请求。
- 卡片已接受、但 Bridge 还没有确认 IDE 时显示 `GOAL: WAIT IDE` 和
  `Waiting for IDE...`。
- Bridge 确认后显示 `GOAL: CODEX / <session>` 或对应的 Trae 会话，并进入任务
  状态展示。
- 有审批请求时，任务摘要区域显示 `APPROVAL: <summary>`，底部改为
  `OK approve  UP reject`，避免用户误把普通按键当成审批。
- 事件行由 DidTiboRest 切片使用。为空时整行折叠；非空时也不允许遮住审批
  横幅或任务摘要。
- `DOWN` 进入第二页查看当前 Skill/Goal 的详细信息；它只切换本地页面，不会
  改变服务状态。

页面已经接入 Passport Service Core、主机连接状态、电量读取和真实卡片事件入口。
NFC Reader/手机中继、IDE 适配器和录音传输仍然是独立的后续切片，不在页面层伪造。

## 7.1 按键手势总表

所有切片的按键手势都必须登记在这里。新切片在申领手势前必须先编辑本表，
避免 DidTiboRest、审批和页面导航在同一条三键分压梯上相互抢占。

| 手势                 | 状态             | 动作                             | 归属切片      |
| -------------------- | ---------------- | -------------------------------- | ------------- |
| `UP` 短按            | 任务视图         | （保留）                         | Service       |
| `UP` 短按            | 审批待处理       | 拒绝审批                         | Service       |
| `DOWN` 短按          | 任务视图         | 切换 Skill/Goal 详情页           | Service       |
| `OK` 短按            | 任务视图         | 启动 / 停止语音采集              | Service       |
| `OK` 短按            | 审批待处理       | 通过审批                         | Service       |
| `OK` 短按            | 通知待处理       | 屏蔽当前通知事件                 | DidTiboRest   |
| `OK` 长按 (>= 1 s)   | 任务视图         | 循环切换 Codex level             | DidTiboRest   |
| `UP` 长按 (>= 1 s)   | 任意             | （保留给后续切片）               | —             |
| `DOWN` 长按 (>= 1 s) | 任意             | （保留给后续切片）               | —             |

说明：

- 通知和审批不能同时占用同一状态。两者同时挂起时，审批优先接管 `OK 短按`，
  通知先保持静音，直到审批结束。
- 除审批状态外，任何切片都不得覆盖 `UP 短按`，以便未来导航能扩展而不破坏
  肌肉记忆。
- 切片 F（Physical Skills MVP）在 `WEAR` / `COMPOSE` 页面新增了按页面限定的
  手势行，具体见
  [`physical-skills-mvp-design.zh_CN.md §8`](physical-skills-mvp-design.zh_CN.md#8-按键手势补充)，
  这些行只在所在页面胜出。

## 8. 实现切片

### 切片 A：Service Core

实现纯 C 状态迁移、有界解析、单卡准入、Goal 确认、审批和主机测试。

### 切片 B：真实传输适配器

实现 USB Serial/JTAG 分帧和能打印/发送协议行的主机客户端。设备路径不包含自动
fixture 或 `--demo` 模式。

### 切片 C：本地 IDE Bridge

定义当前 IDE、会话标识、Goal 激活、任务进度、审批和语音投递的适配器契约。只有
在确认 Trae/Codex 的本地控制面后，才实现第一个适配器；不能臆测未公开的 API。

### 切片 D：NFC 与语音硬件

选择精确的 NFC Reader 或手机中继，再加入 Reader 事件适配器和 BLE/USB 音频链路。
修改 BSP 前必须记录 Reader 型号、总线、接线和功耗预算。

语音默认值（以实测为准）：BLE notification 20 ms、16 kHz 单声道帧；根据主机 CPU
预算选择 Opus 或 G.711。USB Serial/JTAG 保留原始 PCM，作为实验室调试回退。

#### NFC 路径决策表

| 选项                 | 额外硬件         | BSP 变更            | 用户动作                | 首次成本 |
| -------------------- | ---------------- | ------------------- | ----------------------- | -------- |
| 外置 NFC Reader      | PN532 / RC522    | 总线 + 引脚 + 电源  | 直接在 Passport 贴卡    | 高       |
| 手机→Bridge 中继     | 无               | 无                  | 手机读 NFC → Bridge     | 低       |

在切片 D 开工的同一 PR 内必须选定其中一种。两种方案本身都被允许，前提是记录
清楚硬件事实。

### 切片 E：通知伴侣（DidTiboRest）

复用 Service Core、传输、页面布局和按键手势总表。新增
[`did-tibo-rest-idea.zh_CN.md`](did-tibo-rest-idea.zh_CN.md) 定义的 `notify.*`
消息集，以及切片 D 引入的音频播放路径和上文事件行。切片 E 依赖切片 D 的音频
通路，不允许独立于 Passport Service 发布。

### 切片 F：Physical Skills MVP

复用 Service Core、传输和按键手势总表。新增 `WEAR` / `COMPOSE` 页面族、
`task.event*`、`tile.stack.state`、`context.composed`、`skill.updated`、
`skill.pin`、`skill.reload` 和 `voice.capture.*` 消息集。完整 UI、页面迁移和
协议细节见 [`physical-skills-mvp-design.zh_CN.md`](physical-skills-mvp-design.zh_CN.md)。
切片 F 依赖切片 A/B 提供传输、切片 D 提供音频 worker、切片 C 提供真实任务
事件；切片 C 未就绪时允许使用本地 mock。Tile 硬件识别（PN532 或备选）刻意
不纳入本切片。

## 9. 验收与当前阻塞

Service 门禁通过的条件是：真实输入事件而不是 fixture 产生一次 Goal 请求；Bridge
确认所选会话；任务进度和审批正确显示；按键动作只发出一次。

当前剩余阻塞在状态机之外：

1. 当前板卡没有文档化的 MCU 侧 NFC Reader。
2. Trae 和 Codex 的适配器控制面还没有选定。
3. 本固件还没有集成无线音频/事件传输。

这些问题必须和主机测试、固件构建分开报告；构建成功不等于真机验证完成。

## 10. 仓库拆分策略

本仓库是唯一的固件基线。新切片默认落在 `main/` 内，复用前文定义的 envelope、
Service Core、传输、页面布局和按键手势总表。只有当以下任一触发条件成立且
有据可查时，才把切片拆到独立的 git 仓库；仅有文档层面的差异不构成触发条件。

### 固件侧拆分触发条件

只有以下条件被实测并记录，切片才允许作为独立固件搬出本仓库：

1. 该切片依赖当前基线无法容纳的硬件，例如另一种音频 codec、另一种显示驱动、
   另一种 Flash 尺寸，或与基线 NVS/PHY/factory 布局冲突的分区表。
2. 该切片存在独立的发布节奏，且反复错过本仓库的验证门禁，共享发布会让任一
   侧阻塞超过一个周期。
3. 出现第三个可比切片，且两个新切片都需要 Passport Service 有意不承担的共享
   抽象。

在这之前，即便功能面很大，切片也留在本仓库。"是不同的产品想法"本身不是拆分
理由。

### 主机侧拆分策略

Host 桥和 IDE/服务适配器（Codex、Trae、Tibo 以及未来的新适配器）预期都不在
本固件仓库内。它们可以共用一个 `*-passport-bridge` monorepo，也可以每个
适配器一个独立仓库，但都不归本仓库所有。它们从本仓库消费的唯一契约是第 4 节
的协议 envelope 和 `type` 列表；该契约由本仓库版本化管理，下游以 vendor 形式
引用。

### 拆分前必须记录

任何拆分 PR 必须在描述里写清：

- 命中哪一条触发条件，附上实测证据。
- 拆分时点冻结的 envelope 版本和 `type` 列表。
- 新仓库将运行的验证门禁（`validate.sh --static`、host tests、固件构建、
  device tests），确保硬件证据不会因为跨仓而丢失。
- 是否引起分区、BSP 引脚或电源决策变化。若涉及，必须先在本仓库落地。

DidTiboRest 切片目前不满足任何固件侧触发条件，切片 E 留在本仓库；其 host 桥
按上文主机侧策略处理。
