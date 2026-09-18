<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 工程规范（Development）

本目录存放 AI Passport 的工程开发规范和可复用工作流，按用途分组：AI 开发工作流（`ai-guide.md`）、工程约定（`engineering/`）、CI 文档（`ci/`）、发布/完成流程（`release/`）。

## 收录标准

- 收录构建验证、代码风格、注释、测试、资源约束和 AI 开发流程。
- `ai-guide.md` 面向 AI 编程助手，允许包含本项目结构和硬件边界。
- 每条规则应写清触发条件、必须做什么、禁止做什么、验证方法和例外条件。
- 涉及本板具体硬件事实的结论引用 `docs/hardware-design/`，不重复。
- 可以由 lint、测试或脚本强制的要求，应同时落实到自动化检查，不能只靠 agent 阅读文字。
- 新增规约时在本文件更新索引。

## AI 工作流

- [ai-guide.zh_CN.md](ai-guide.zh_CN.md)：AI 开发工作流（面向 AI 编程助手：上下文建立、需求拆解、BSP 边界、验收交付格式）。

## 工程约定（engineering）

- [environment-setup.zh_CN.md](engineering/environment-setup.zh_CN.md)：AI 在全新机器上的环境引导，包含国际与中国大陆下载线路。
- [build-and-test.zh_CN.md](engineering/build-and-test.zh_CN.md)：构建与验证（ESP-IDF 命令、逻辑测试、改动验证要求）。
- [firmware-layout.zh_CN.md](engineering/firmware-layout.zh_CN.md)：默认/用户自定义分区布局与合并产物验证。
- [coding-conventions.zh_CN.md](engineering/coding-conventions.zh_CN.md)：代码约定（语言风格、复用、注释、测试同步、资源约束等）。

## 软件架构

- [passport-pixel-ui-design.zh_CN.md](passport-pixel-ui-design.zh_CN.md)：当前 v2 视觉设计，包含 24 个界面预览、操作确认卡、图形电量、会话路由和 P1 桌面宠物同步。
- [passport-v2-protocol.zh_CN.md](passport-v2-protocol.zh_CN.md)：会话路由、审批回执和自动伙伴传输的 v2 通信契约。
- [passport-service-architecture.zh_CN.md](passport-service-architecture.zh_CN.md)：Passport Service 边界、协议、状态模型以及真实 NFC/IDE 接入门禁。
- [passport-service-status.zh_CN.md](passport-service-status.zh_CN.md)：最近一次 Passport 烧录、启动、验证结果和当前边界记录。
- [passport-service-todo.zh_CN.md](passport-service-todo.zh_CN.md)：Passport Service MVP 的唯一后续开发清单。
- [physical-skills-mvp-design.zh_CN.md](physical-skills-mvp-design.zh_CN.md)：切片 F Physical Skills MVP —— Wear 与 Compose 页面设计、协议扩展和按键手势补充。
- [nfc-skill-card-design.zh_CN.md](nfc-skill-card-design.zh_CN.md)：可写 NFC Skill 卡协议、Passport Host Service 归属、免费版 NFC Tools MVP 流程和 V2 GitHub 来源方案。
- [ide-adapter-decision.zh_CN.md](ide-adapter-decision.zh_CN.md)：切片 C 决策记录 —— 第一个本地 IDE 适配器选 Codex，Trae 暂缓。
- [nfc-path-decision.zh_CN.md](nfc-path-decision.zh_CN.md)：切片 D 决策记录 —— NFC 输入首选手机→Bridge 中继，外置 Reader 暂缓。
- [softap-fallback-decision.zh_CN.md](softap-fallback-decision.zh_CN.md)：SoftAP + TCP 备用链路保留代码但默认不启用。

## 探索中的想法

- [did-tibo-rest-idea.zh_CN.md](did-tibo-rest-idea.zh_CN.md)：把 Tibo 推送提醒和
  Codex level 控制放到 AI Passport 上的探索记录；文中明确当前原型不作为实现基线。

## CI（ci）

- [CI-validation.zh_CN.md](ci/CI-validation.zh_CN.md)：Pull Request 与 main 的自动仓库检查、host tests 和固件验证。
- [CI-build-and-release.zh_CN.md](ci/CI-build-and-release.zh_CN.md)：自动构建与发布说明（tag 触发自动编译固件并发布 Release）。
- [CI-sync-main.zh_CN.md](ci/CI-sync-main.zh_CN.md)：上游同步说明（定期把上游 `FoloToy/ai-passport` 的 `main` 同步到本 fork 的 `main`）。

## 发布/完成流程（release）

- [publish-to-community.zh_CN.md](release/publish-to-community.zh_CN.md)：发布到社区说明（把当前固件发布到 AI Passport 社区市场）。
- [project-completion.zh_CN.md](release/project-completion.zh_CN.md)：项目开发完成流程说明（一组可选收尾动作）。
- [file-issues.zh_CN.md](release/file-issues.zh_CN.md)：提交 issue 说明（把建议作为上游 GitHub issue 提交）。
