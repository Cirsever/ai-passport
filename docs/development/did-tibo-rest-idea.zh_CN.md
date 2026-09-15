<p align="right">
  <strong>简体中文</strong> · <a href="did-tibo-rest-idea.md">English</a>
</p>

# 把 DidTiboRest 搬到 AI Passport

状态：探索中的想法。当前固件原型不作为后续实现基线。

## 项目想法

把 AI Passport 做成 Tibo 推送的实体提醒设备。Tibo 有新推送时，Passport
发出声音，提醒用户有待处理的事情。如果用户没有操作，设备按设定的间隔
再次提醒。用户可以用按键屏蔽当前提醒，也可以一键切换 Codex 的 level
等级。

## 预期动线

1. Tibo 向主机侧 Bridge 发送推送事件。
2. Bridge 把事件转发给 Passport。
3. Passport 展示简短的标题和消息，并播放一段提示音。
4. 如果事件还没有处理，Passport 在提醒间隔到达后再次播放提示音。
5. 用户可以屏蔽当前事件，或者切换 Codex level。
6. 主机侧返回操作结果，确认动作完成后清除待处理提醒。

## 还需要确定的问题

- Tibo 真实的推送通道和事件格式。
- Codex level 有几档、每一档代表什么，以及主机侧如何切换。
- 屏蔽只对当前事件生效，还是对整个会话或一段安静时间生效。
- 提醒间隔、最多重试次数、是否升级提醒，以及音量规则。
- 按键手势如何与 Passport 的全局导航共存。
- 第一版接入 USB、TCP、BLE，还是使用可替换的传输接口。
- 是否需要跨重启保存待处理状态。

## 当前原型为什么不能作为基线

当前原型使用的是本地 JSON 格式和 USB 开发帧，还没有接入经过确认的 Tibo
真实推送，也没有真正执行 Codex level 切换，更没有验证完整的操作确认闭环。
开机路由和诊断回执只是为了方便做硬件探测，不能代表最终产品动线。提醒
策略和按键语义也还没有经过产品确认。

## 后续重新开始的位置

下一次实现应先确定主机侧事件契约，并用纯状态机补齐测试矩阵。设备传输、
声音、屏幕和按键都作为这个契约外面的适配层。第一轮硬件验收至少要覆盖：
真实推送、重复提醒、屏蔽、level 切换、操作确认和断线重连。

## 与 Passport Service 的集成方式

本想法不是独立固件，而是 Passport Service 架构中的切片 E
（见 [`passport-service-architecture.zh_CN.md`](passport-service-architecture.zh_CN.md#切片-e通知伴侣didtiborest)），
必须复用既有的协议 envelope、页面布局和按键手势总表。

### 共用协议 envelope

所有通知消息都走与 `task.state` / `goal.mode.*` / `approval.*` 相同的
`@passport ` 单行 JSON 通道。不允许另外造一层 host↔device 协议；新增 `type`
必须先落到 Passport Service 协议章节。

主机发送给 Passport：

```json
{"type":"notify.push","event_id":"tibo-42","title":"Tibo","summary":"Action waiting","sound":"chime","reminder_ms":60000,"max_repeat":3}
{"type":"notify.level","level":"L2"}
{"type":"notify.state","event_id":"tibo-42","state":"cleared"}
```

Passport 发送给主机：

```json
{"type":"notify.mute","event_id":"tibo-42","scope":"event"}
{"type":"notify.level","level":"L2","source":"button"}
```

规则：

- 未知字段忽略；未知 `type` 拒绝并不修改状态，沿用 Passport Service 既有规则。
- 已在跟踪的 `event_id` 收到新的 `notify.push` 会覆盖当前记录，不叠加。
- `notify.state=cleared|resolved|failed` 释放提醒定时器并折叠通知行。

### UI 布局

通知占用 Passport 页面布局中的可折叠事件行
（见 [第 7 节](passport-service-architecture.zh_CN.md#7-passport-页面信息布局)）。
事件行不能覆盖任务摘要或审批横幅；审批挂起期间通知行保留，但提醒声音抑制，
直到审批结束。

### 按键手势

通知和 Codex level 相关手势已经登记在
[第 7.1 节按键手势总表](passport-service-architecture.zh_CN.md#71-按键手势总表)：

- 通知待处理时 `OK` 短按屏蔽当前事件。
- 任务视图下 `OK` 长按（>= 1 秒）循环切换 Codex level，并发出
  `notify.level`，`source=button`。
- `UP` 和 `DOWN` 短按保留 Passport Service 的既有语义。

未在总表登记前不得引入新手势。

### 音频与硬件依赖

提示音复用切片 D 中引入的 ES8311 音频 worker。本切片不新增音频通路、不影响
NFC 路径决策、不修改 BSP 接线。切片 E 依赖切片 D 的音频通路。

### 持久化与重启

最多只将当前挂起的 `event_id`、剩余重复次数和最近一次 Codex level 落到 NVS，
使用共享命名空间，避免污染既有分区表和 PC Wi-Fi 状态。如果本切片暂时不接入
NVS，设备在重启后丢弃待处理提醒，等待 Bridge 重放。

### 切片 E 验收

- 一条 `notify.push` 恰好触发一次提示音和一行通知。
- 长时间未确认时按 `reminder_ms` 重复，最多重复 `max_repeat` 次。
- 通知期间 `OK` 短按发出一条 `notify.mute` 并停止重复。
- `OK` 长按发出一条 `notify.level`，由 Bridge 用主机侧 `notify.level` 回执。
- 审批挂起时抑制提示音但不删除通知行。
- Bridge 离线时重启：设备不能出现幽灵通知。
