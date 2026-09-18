<p align="right">
  <a href="passport-service-status.md">English</a> · <strong>简体中文</strong>
</p>

# Passport Service 开发状态

状态日期：2026-09-18

分支：`codex/passport-service-mvp`

当前阶段：Passport v2 基线已经开发完成。固件、Codex 主机会话路由、审批回执、
图形电量、会话选择器和 PC 宠物自动同步已经接通，仓库完整门禁通过，v2 固件
已经烧录。本轮下一步是实机走查。

本文记录经过验证的项目状态，不是版本发布说明。

## 已实现

- 原有像素场景、3.2 秒贴卡扫描、四卡叠放和松键停止录音保持不变。
- 设备会定期发送 protocol 2 hello。16 位 Bridge 标识变化后，旧路由、待处理
  审批和暂存伙伴资源立即失效。
- `passport_v2_state` 只保存三条会话记录；目录收齐后才整体发布。旧的切换事务
  不会覆盖新选择，所有操作都按 `bridge`、不透明 `sid` 和 `epoch` 路由。
- 长按上键打开会话选择器，上／下键移动，确认键发起切换。审批和录音期间不能
  切换；一段录音从开始到结束固定使用同一条路由。
- 审批改为操作卡，并提供详情页。设备必须等到 `approval.receipt`，只发出决定
  不会显示为执行成功。
- 顶栏电量改为四格像素图标。电量不可用时显示 `?`，不会把 USB 连接误判为充电。
- Codex 适配器同时运行兼容用 MCP 客户端和真实 app-server 客户端。
  `thread/list`、`thread/read`、`thread/resume`、`turn/start` 用于精确路由已有
  会话。本机只读 smoke 已成功返回当前仓库的一条真实 thread。
- app-server 的命令／文件审批和旧 MCP elicitation 都能转成 Passport 审批，
  回写时分别使用各自的原生 decision 枚举。
- 用户只需在 PC IDE 换宠。Bridge 读取 Codex 当前选择，防抖 500 ms，将已验证的
  v2 图集转换为一张 32x32 索引图，并只在链路空闲时传输；设备没有换宠或手动同步
  入口。
- 伙伴资源按 128 字节分块停等传输，带偏移 ACK 和 SHA-256 commit。设备只在非
  活动槽完整校验后切换，失败时继续显示旧伙伴。
- Trae 仍明确降级为 protocol-1 只读／合成会话模式，因为现有 CLI 不能证明能
  精确路由到指定的历史对话。

协议以 [`passport-v2-protocol.zh_CN.md`](passport-v2-protocol.zh_CN.md) 为准，
界面和交互以
[`passport-pixel-ui-design.zh_CN.md`](passport-pixel-ui-design.zh_CN.md) 为准。

## 资源和构建数据

- `s_v2` 静态 RAM：2,632 字节。
- `s_scene` 静态 RAM：760 字节，其中包含绘制用的当前索引伙伴。
- 应用镜像：1,596,944 字节。
- factory 分区：8,323,072 字节，剩余约 81%。
- 最新合并镜像：`build/FoloToy-AI-Passport-full.bin`，1,662,480 字节。
- SHA-256：
  `da49e579bcd3072e0225c626954d95857407e7955296e3adab68c5b7e491f61d`。
- 默认分区仍是 NVS、PHY 数据和单个 factory 应用，没有新增产品分区。

## 验证结果

v2 开发完成后已执行：

- `./tools/validate.sh --static`：PASS。
- Host C tests：PASS，覆盖会话目录事务、路由隔离、审批回执、伙伴偏移／commit
  和电量边界。
- Python tests：PASS。系统 Python 下通过 14 个 Codex 适配器用例、4 个 Bridge
  接线用例和 4 个 Pillow 宠物转换／防抖用例。
- 真实 Codex app-server 只读 smoke：PASS。
- ESP-IDF 5.5.3 固件构建：PASS。
- 合并镜像布局校验：PASS。
- 使用固件同一 `passport_scene_draw` 的 Host 预览：PASS，覆盖空闲、扫描、
  多卡和索引伙伴。

ESP-IDF 自带的 Python 环境没有可选 Pillow，因此其中 4 个伙伴转换用例会显示
skip；Bridge 使用的系统 Python 已安装 Pillow 10.4.0，同一组用例全部通过。

## 烧录与实机状态

- 当前识别到的设备：`/dev/cu.usbmodem2101`。
- 上一版 Slice F 已烧录，并完成松键停止录音的实机验证。
- v2 烧录前已执行 `./tools/validate.sh --preflash`，结果 PASS。
- 已验证的 bootloader、分区表和 1,596,944 字节应用分别写入 0x0、0x8000 和
  0x10000；三段设备端 hash 校验全部通过，RTS 硬复位完成。0x9000 的 NVS 区域
  未被覆盖。
- 重启后，真实 USB Bridge 收到 protocol-2 `device.hello`，并回传
  `host.hello` 和当前空路由快照；Codex MCP 0.139.0 与精确路由 app-server
  均启动成功。

## 尚待验证

- 在 240x320 实体屏上检查 v2 电量、会话选择器、操作卡和伙伴显示。
- 走一条真实 app-server 审批，确认设备端回执状态。
- 在 PC IDE 更换宠物，确认空闲时自动同步。如果 IDE 没有暴露当前宠物选择键，
  Bridge 应继续保留设备上的旧伙伴。
- 在四卡、录音和伙伴传输同时存在时测量运行时剩余堆内存。静态符号大小不能代替
  峰值 RAM 测量。
- 完成真实 NFC 和外部 STT 的剩余验证。

## 恢复点

v2 烧录后，从 [`passport-service-todo.zh_CN.md`](passport-service-todo.zh_CN.md)
的实机走查继续。不要增加设备端换宠或手动同步入口。
