<p align="right">
  <strong>简体中文</strong> · <a href="passport-v2-protocol.md">English</a>
</p>

# Passport v2 通信契约

[像素伙伴设计](passport-pixel-ui-design.zh_CN.md)的实现契约。
所有帧均为扁平 UTF-8 JSON 对象，不含 USB `@passport ` 前缀和换行时小于
512 字节。下文数字均为无符号 32 位整数，字符串正确 JSON 转义，不截断 UTF-8 字符。

## 连接和路由

`host.hello` 声明 `protocol:2`、`bridge`（16 位小写十六进制）、`sessions`、
`companion` 布尔值。Bridge 改变时，原路由、请求和暂存资源失效。
设备定期发送 `device.hello`，主机按需重发身份与当前路由。

`session.query` 携带 `tx`；`session.select` 增加 `sid`；`session.cancel` 使旧
事务失效并要求核对当前选择。同一连接内事务递增，旧事务不能覆盖新的选择。
主机通过 `session.selected` 返回 `tx`、`bridge`、`epoch`、`sid`、`ide`、
`title`、`state`、`summary`、`progress` 和 `writable`。
空 `sid` 表示没有当前会话，完整快照被接受后才改变界面路由。
切换失败通过 `session.error` 返回 `tx` 和 `reason`，超时重新查询。

每条会话任务、审批和操作都携带 `bridge`、`sid`、`epoch`。
标识不透明，完整原生 thread ID 留在主机。显示用短编号不参与路由。
v2 协商后，拒绝没有会话范围的旧版任务和审批帧。

## 会话目录和审批详情

`session.list` 请求从零开始的 `page` 和 `tx`。主机先发送 `session.catalog`
（`tx`、`page`、`count` ≤3、`total`），再发送恰好 `count` 条 `session.entry`，
携带同一 `tx`、`index`、`sid`、`title`、`ide`、`state` 和 `writable`。
设备暂存整页，完整后才展示。重复或迟到的记录不能修改新的一页。

`approval.request` 携带路由、`request_id`、`operation`（`edit`、`command`、
`review`）、`summary`、`pages`、`allow`、`remaining_ms`。
只有完整可信的范围才启用 `allow`。设备以 `approval.detail` 请求一页，
主机用 `approval.page` 返回路由／请求身份、`page`、`pages`、`text`。
完整详情由主机保存。

`approval.decision` 携带路由／请求和 `decision`（`approve` 或 `reject`）。
界面等待 `approval.receipt`，其 `status` 为 `allowed`、`denied`、`expired`、
`unknown`。回执表示适配器已经向原生请求通道提交决定，不证明任务已经执行。
不提供会话级永久授权。回执丢失后需要查询状态。

## 自动伙伴传输

用户只在 PC IDE 换宠。适配器检测选中资源的版本，在本地转换后利用空闲通信
传输。设备自动 ACK，不提供面向用户的同步命令。

`companion.begin` 携带路由、`asset`（SHA-256 十六进制）、`bytes`、`frames`
和 `name`。当前基线要求 `frames:1`，最多四帧留待后续能力协商。
资源包前 32 字节为小端 RGB565 调色板，后面为 `frames * 512` 字节的逐行
4 位索引，高半字节在前。索引 0 透明，每帧 32 × 32。

`companion.chunk` 包含 `asset`、`offset` 和 `data`（不超过 128 字节原始数据
的 base64）；`companion.commit` 请求完整校验。每次响应为 `companion.ack`，
包含 `asset`、`offset`、`status`（`receiving`、`ready`、`retry`、`rejected`）。
发送方重试未确认的偏移，并检查接收方偏移。只有长度和 hash 全部正确才替换
当前资源。失败时保留原伙伴，忽略旧路由的传输片段。
传输和存储工作不在按键回调中执行，审批和语音优先于资源分块。

## 兼容性

旧版 protocol-1 适配器保留单会话行为。发送带路由操作前必须协商 v2 能力。
不支持精确会话的 IDE 后端只读展示，不把合成对话 ID 当作可路由会话。
默认分区布局保持不变。
