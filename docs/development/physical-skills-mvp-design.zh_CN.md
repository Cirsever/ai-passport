<p align="right">
  <strong>简体中文</strong> · <a href="physical-skills-mvp-design.md">English</a>
</p>

# AI Passport Physical Skills MVP — 页面设计

状态：MVP 设计稿，源自 `AI_Passport_Physical_Skills_MVP_Design_v0.1`。
范围：在当前 Passport 硬件基线上，交付 Wear 模式和 Compose 模式的页面布局、
页面级状态迁移、协议扩展和按键手势。非 UI 逻辑（Host 侧 Skill Registry、
热更新引擎、Tile Reader 硬件、机械设计）不在本文档范围内，归属 Host 侧
项目。

本文档假设并不再重复：

- 硬件基线：ESP32-C3 + ST7789P3 240×320 + ES8311 + CW2017 + 三按键 ADC 分压梯
  （见 [`AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md`](../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md)）。
- 协议 envelope：`@passport ` 单行 JSON
  （见 [`passport-service-architecture.zh_CN.md §4`](passport-service-architecture.zh_CN.md#4-服务协议)）。
- 传输、Service Core、超时和仓库拆分规则沿用同一份架构文档。

## 1. 范围与非目标

范围内：

- Wear / Compose 屏幕框架和页面清单。
- Wear 与 Compose 之间的模式切换。
- 每个页面的按键手势。
- 设备端消费和发出的协议消息类型。
- 语音、审批和 Skill 热更新在设备端的反馈表面。

非目标（归 Host 或后续切片）：

- Skill Registry、semver/hash 校验、文件监听。
- PN532 / RC522 固件、驱动或 Tile Reader 机械件。
- 除 envelope 已定义之外的 Codex/Trae 适配器行为。

## 2. 模式与切换规则

两种模式：

- `WEAR`（随身）：Passport 是正在运行的 Agent 任务的伴随显示。
- `COMPOSE`（组合）：Passport 显示当前物理 Skill 堆叠和解析后的 Context。

页面上模式标识以中文显示：`随身` / `组合`。文档中提到状态机时保留英文
标识（`WEAR` / `COMPOSE`），仅用于协议和代码内部。

自动规则：

- Tile 堆叠为空 → `WEAR`。
- Tile 堆叠 ≥ 1 → `COMPOSE`。

手动覆盖：

- `DOWN` 长按（≥ 2 秒）切换 `mode.lock`。锁定期间自动模式切换被抑制，直到用户
  解锁或堆叠进入必须处理的状态（例如 Compose 锁定期间收到审批仍会覆盖显示
  审批浮层）。

Header 始终显示当前模式；用户锁定模式时，Header 显示挂锁字形。

## 3. 屏幕框架

每个页面共用固定框架，便于形成一致的肌肉记忆：

```text
┌─────────────────────────────┐  0
│  顶栏 24 px                 │  状态 / 模式 / 电量
├─────────────────────────────┤  24
│                             │
│  正文 272 px                │
│                             │
├─────────────────────────────┤  296
│  提示条 24 px               │  按键提示
└─────────────────────────────┘  320
```

布局规则：

- 所有 LVGL 访问放在 `bsp_lvgl_lock()` 内。
- 顶栏和提示条由单一 page frame 控件持有；页面正文不能直接重绘它们，
  避免模式 / 电量 / 提示被页面代码覆盖。
- 浮层（审批、语音采集、瞬时提示）只占用正文区域，除非明确说明；顶栏与
  提示条保持可见。
- 页面上英文占位以中文替代（如 `随身` / `组合` / `任务` / `事件` / `卡` /
  `目标` / `聆听中` / `已断开` / `技能` / `已更新`），协议侧的 `type` / `state`
  值等 wire schema 名称保留英文。

## 4. Wear 模式页面

### 4.1 `WEAR.HOME`

Wear 模式的落地页，堆叠为空时显示。

```text
┌─────────────────────────────┐
│ 随身  已连接              电▮ │
├─────────────────────────────┤
│                             │
│ 运行中  42%                 │
│ 重构 tracing                │
│                             │
│ 事件  分析器完成汇总        │
│                             │
│ 卡    card-1                │
│ 目标  CODEX / session-1     │
│                             │
├─────────────────────────────┤
│ 上 任务  下 组合  确 长按说话│
└─────────────────────────────┘
```

数据来源：`task.state`、最近的 `task.event`（见 §7）、`goal.mode.state`、
Service Core 里的 `nfc` 身份。顶栏中文标识说明：

- `随身` / `组合`：当前模式。
- `已连接` / `未连接` / `断线`：主机链路状态。
- `电▮`：电量图标，具体数值在低电量或充电时展开为 `电 42%`。

### 4.2 `WEAR.TASK`

任务详细视图，展示滚动事件日志（最近 5 条）。

```text
┌─────────────────────────────┐
│ 随身  任务 runtime-1      电▮ │
├─────────────────────────────┤
│ 运行中 42%                  │
│ 跨包重构 tracing            │
│ pkg/foo/bar/**              │
│                             │
│ 12:30 分析器开始扫描        │
│ 12:31 发现 3 处问题         │
│ 12:33 补丁草稿就绪          │
│ 12:35 正在运行测试…         │
│                             │
├─────────────────────────────┤
│ 上 返回  下 目标  确 已阅   │
└─────────────────────────────┘
```

`确认键短按` 确认最顶部的事件（设备发出 `task.event.ack`）；这与语音采集是
不同的动作，语音采集只在 `WEAR.HOME` 页面。

### 4.3 `WEAR.APPROVAL`（浮层）

由 `approval.request` 触发；在任意 Wear 页面的正文区显示浮层。

```text
┌─────────────────────────────┐
│ ! 审批                    电▮ │
├─────────────────────────────┤
│ 将写入 3 个文件             │
│ 风险  写文件                │
│                             │
│ 文件                        │
│   src/net/http.c            │
│   src/net/http.h            │
│   src/util/log.c            │
│                             │
├─────────────────────────────┤
│ 上 拒绝  下 详情  确 通过 ✓ │
└─────────────────────────────┘
```

沿用架构 §4.2 的 60 秒审批超时规则。浮层不遮挡顶栏，因此审批过程中模式和
电量仍然可见。

### 4.4 `WEAR.VOICE`（浮层）

`WEAR.HOME` 上 `确认键` 长按（≥ 300 ms）触发；松开结束。

```text
┌─────────────────────────────┐
│ 随身  聆听中              电▮ │
├─────────────────────────────┤
│                             │
│    ▮ ▮▮▮ ▮▮▮▮▮ ▮▮▮ ▮        │
│    ------- 0:04 --------    │
│                             │
│ "现在做到哪了？"            │
│                             │
├─────────────────────────────┤
│ 上 取消     松开 确认 结束  │
└─────────────────────────────┘
```

音频 worker 在独立任务中运行；LVGL 任务仅根据 worker 发布的峰值刷新电平条。

### 4.5 `WEAR.DISCONNECTED`

桥断线时的整屏状态。顶栏仍然显示 `断线`。

```text
┌─────────────────────────────┐
│ 随身  断线                电▮ │
├─────────────────────────────┤
│                             │
│ ⚠ 与主机失联                │
│ 最后一次同步 12:30          │
│                             │
│ 请保持随身或返回底座        │
│                             │
├─────────────────────────────┤
│ 上 重试     下 快照         │
└─────────────────────────────┘
```

设备不伪造状态。快照视图展示最后一次已知的 task/event/goal 值，并附带
`过期起自 HH:MM` 提示。

## 5. Compose 模式页面

### 5.1 `COMPOSE.STACK`

Compose 落地页。从下到上对应物理堆叠的实际顺序。

```text
┌─────────────────────────────┐
│ 组合  3 张                电▮ │
├─────────────────────────────┤
│ 顶 ─────                    │
│   ▶ [ 审查    v0.3.2 ]      │
│     [ 项目    aide  ]       │
│     [ 主体    v0.4.1 ]      │
│ 底 ─────                    │
│                             │
│ 上下文  主体 + 项目 + 审查  │
│ 状态    已组合 640 ms       │
├─────────────────────────────┤
│ 上 随身  下 选下  确 打开   │
└─────────────────────────────┘
```

Tile 名称按角色映射到中文短标签：`主体` = `AIDE`（Agent 主体）、
`项目` = `PROJECT`、`审查` = `REVIEW`。原始 `skill_id` 保留在协议里。

选择指针 `▶` 从最顶 Tile 开始，按 `下键短按` 向下移动，最后一格后回到顶部。
`确认键短按` 进入所选 Tile 的 `COMPOSE.SKILL` 页。

组合状态行显示最近的 `context.composed` 结果，或者当 Resolver 拒绝时显示
红色 `冲突`。

### 5.2 `COMPOSE.SKILL`

当前所选 Tile 对应 Skill 的详情。

```text
┌─────────────────────────────┐
│ 组合  技能 审查          电▮ │
├─────────────────────────────┤
│ 标识      review            │
│ 版本      0.3.2 · 已锁定    │
│ 接受      code, project     │
│ 工具      git.diff          │
│           code.search       │
│ 写文件    否                │
│ Shell     需确认            │
│                             │
├─────────────────────────────┤
│ 上 返回  下 重载  确 锁定   │
└─────────────────────────────┘
```

字段来自 Host 发的 `skill.updated`。设备不计算 prompt 内容，只镜像 revision id
和权限摘要。锁定与解锁在 hint 中以 `已锁定` / `未锁定` 显示。

### 5.3 `COMPOSE.RELOAD`（瞬时提示）

Host 发出 `skill.updated` 且对应 Skill 在当前堆叠中时，弹出 2 秒瞬时浮层。

```text
        ┌──────────────────────┐
        │ 技能已更新           │
        │ 审查 0.3.2 → 0.3.3   │
        │ 会话已锁定当前版本   │
        │ 确 采用   上 保持    │
        └──────────────────────┘
```

如果用户不操作，浮层消失，Session 保留原锁定版本，对应设计稿中"默认 session
锁定版本，除非用户显式 reload"的规则。

## 6. 页面迁移图

```text
        approval.request                approval.decision
             │                                │
             ▼                                │
      [WEAR.APPROVAL] <────────────────────── │
             ▲                                │
             │                                │
[DISCONNECTED] ─ 链路恢复 ─▶ [WEAR.HOME] ◀─ 上键 ─ [WEAR.TASK]
             ▲                    │  ▲
             │  链路断             │  │ 长按确认键
             └────────────────────┤  ▼
                                  │ [WEAR.VOICE]
       stack ≥ 1                  │
             ▼                    │
      [COMPOSE.STACK] ◀── 上键 ────┘
             │  ▲
             │  │ 上键
             ▼  │
      [COMPOSE.SKILL]
             │
             ▼ （瞬时）
      [COMPOSE.RELOAD 瞬时提示]
```

`下键` 长按切换 `mode.lock`；锁定期间上图两条 `stack ≥ 1` / `stack = 0` 的
垂直切换被抑制。

## 7. 协议扩展

所有新消息沿用
[`passport-service-architecture.zh_CN.md §4`](passport-service-architecture.zh_CN.md#4-服务协议)
中定义的 `@passport ` 行 envelope。新增 type：

Host → Passport：

```json
{"type":"task.event","task_id":"runtime-1","event_id":"e-9","summary":"analyzer scan started","ts":"12:30"}
{"type":"tile.stack.state","context_id":"ctx-14","stack":[{"tile_id":"aide","role":"agent","skill_id":"aide","revision":"0.4.1"},{"tile_id":"project.aide","role":"project","skill_id":"project","revision":"0.1.0"},{"tile_id":"review","role":"skill","skill_id":"review","revision":"0.3.2"}]}
{"type":"context.composed","context_id":"ctx-14","skills":["aide","project","review"],"duration_ms":640,"status":"ok"}
{"type":"skill.updated","skill_id":"review","revision":"0.3.3","previous":"0.3.2","status":"ready","summary":"Adjust review prompt"}
{"type":"voice.reply","request_id":"v-4","text":"about 42%, tests running"}
```

Passport → Host：

```json
{"type":"task.event.ack","event_id":"e-9"}
{"type":"skill.pin","skill_id":"review","revision":"0.3.2","source":"button"}
{"type":"skill.reload","skill_id":"review","source":"button"}
{"type":"voice.capture.start","request_id":"v-4","sample_rate":16000,"codec":"pcm16"}
{"type":"voice.capture.chunk","request_id":"v-4","seq":0,"payload_b64":"…"}
{"type":"voice.capture.stop","request_id":"v-4","duration_ms":4200,"reason":"release"}
```

规则：

- 未知或格式错误的消息被拒绝，不修改状态。
- `tile.stack.state` 是权威来源：空 `stack` 表示物理堆叠为空，设备立即退出
  Compose。设备不会通过 UID 猜测堆叠。
- 若 `skill.updated` 的 `skill_id` 不在当前堆叠中，仅 Host 侧的 Skill Registry
  状态被更新；设备忽略。
- 语音帧有界（默认 20 ms @ 16 kHz PCM），传输拥塞时按帧丢弃，Host 负责补偿。
- 新增 `type` 必须先落到本文和 architecture 协议章节，然后才能开始实现。

## 8. 按键手势补充

以下行加入
[`passport-service-architecture.zh_CN.md §7.1`](passport-service-architecture.zh_CN.md#71-按键手势总表)。
总表是唯一权威；本节仅列 Physical Skills MVP 的新增行。表中按键沿用硬件
常量名 `UP` / `DOWN` / `OK`，与代码常量一致；页面上呈现给用户的中文标签是
`上` / `下` / `确`。

| 手势                 | 页面 / 状态          | 动作                                     | 归属切片         |
| -------------------- | -------------------- | ---------------------------------------- | ---------------- |
| `OK` 长按 (≥ 300 ms) | `WEAR.HOME`          | 进入 `WEAR.VOICE` 并开始上传音频         | Service          |
| `OK` 松开            | `WEAR.VOICE`         | 停止采集，发送 `voice.capture.stop`      | Service          |
| `UP` 短按            | `WEAR.VOICE`         | 取消采集，不发送数据帧                   | Service          |
| `UP` 短按            | `WEAR.HOME`          | 进入 `WEAR.TASK`                         | Service          |
| `DOWN` 短按          | `WEAR.HOME`          | 若 stack ≥ 1 则进入 Compose              | Physical Skills  |
| `OK` 短按            | `WEAR.TASK`          | 确认最顶事件（`task.event.ack`）         | Service          |
| `DOWN` 短按          | `COMPOSE.STACK`      | 选择指针向下循环                         | Physical Skills  |
| `OK` 短按            | `COMPOSE.STACK`      | 进入所选 Tile 的 `COMPOSE.SKILL`         | Physical Skills  |
| `UP` 短按            | `COMPOSE.STACK`      | 回到 `WEAR.HOME`                         | Physical Skills  |
| `UP` 短按            | `COMPOSE.SKILL`      | 返回 `COMPOSE.STACK`                     | Physical Skills  |
| `DOWN` 短按          | `COMPOSE.SKILL`      | 发送 `skill.reload`                      | Physical Skills  |
| `OK` 短按            | `COMPOSE.SKILL`      | 切换锁定，发送 `skill.pin`               | Physical Skills  |
| `OK` 短按            | `COMPOSE.RELOAD`     | 采纳新版本（放弃当前锁定）               | Physical Skills  |
| `UP` 短按            | `COMPOSE.RELOAD`     | 保留当前锁定                             | Physical Skills  |
| `DOWN` 长按 (≥ 2 s)  | 任意                 | 切换 mode-lock                           | Service          |

架构 §7.1 中已有的行（审批、通知、level）保持不变；上表中按页面限定的行仅
在对应页面内胜出。

## 9. 语音、声音与触觉反馈

- 语音采集：16 kHz PCM16 单声道，20 ms 帧，worker 任务驱动，有界环形缓冲，
  `voice.capture.stop` 中上报丢帧计数。
- 审批 / 通知 / Compose 反馈音共享 Flash 中的一小段波形包；音频回放串行，
  审批提示音不会打断语音回复播放。
- 当前基线没有振动马达，反馈只有音频和屏幕闪烁。屏幕闪烁预算 100 ms，
  不打断 LVGL 帧预算。

## 10. 验收

Physical Skills MVP 在真机上（不使用 fixture）满足以下全部条件才算通过：

1. `WEAR.HOME` 在收到 Host 消息后 250 ms 内展示真实任务状态、事件、目标 和
   NFC 身份。
2. `WEAR.HOME` 的 `OK` 长按产生一次连续语音采集，起止帧正确，无 LVGL 任务
   饥饿。
3. `WEAR.APPROVAL` 浮层正确显示，遵循 60 秒超时，全程不遮挡顶栏 / 提示条。
4. 真实的 3 Tile 堆叠触发 `tile.stack.state`，产生 `context.composed`，
   `COMPOSE.STACK` 在 800 ms 内反映堆叠。
5. `skill.updated` 中若 Skill 在当前堆叠内，弹出重载瞬时提示，且能被两个
   记录中的手势正常关闭。
6. `DOWN` 长按锁定模式；锁定期间堆叠变化不切换模式。
7. Bridge 断线时切到 `WEAR.DISCONNECTED`，展示 “过期起自 HH:MM” 提示，且不
   伪造任何状态。

Build、Host tests、Device tests 按仓库规矩分开报告。

## 11. 待决问题

- Host 侧 Skill Registry 契约的具体字段和权限词汇。本文档只固定设备渲染的
  字段。
- 语音回复的投递方式：Host 合成音频流 vs 只回文字短答。当前 mock 使用文字，
  音频路径随 Slice D 一同落地。
- `mode.lock` 是否跨重启保留。MVP 默认：不保留。
- 堆叠超过 4 张 Tile 时的滚动方案。当前 3 Tile MVP 不需要滚动。

## 12. 在切片路线中的位置

本设计是 [`passport-service-architecture.zh_CN.md §8`](passport-service-architecture.zh_CN.md#8-实现切片)
中 **切片 F：Physical Skills MVP** 的具体 UI / 协议内容。切片 F 依赖：

- 切片 A/B（Service Core + 真实传输）：必需。
- 切片 D 音频 worker：`WEAR.VOICE` 必需。
- 切片 C IDE 适配器：`task.event` 和真实语音回复必需，MVP 阶段可用本地
  mock 顶替。

Tile 硬件（PN532 或备选识别方案）刻意放在切片 F 外，遵循设计稿"Phase 0 先做
物理表征、后做安装结构"的顺序。
