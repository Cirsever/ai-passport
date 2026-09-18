<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Development Guidelines

This directory contains AI Passport engineering rules and reusable workflows, grouped by purpose: the AI-assisted development workflow (`ai-guide.md`), engineering conventions (`engineering/`), CI documents (`ci/`), and the release/completion flow (`release/`). Rules should identify their trigger, required action, prohibited action, validation, and exceptions. Hardware facts belong in `docs/hardware-design/`; automatable requirements must also be enforced by tooling or CI.

## AI workflow

- [ai-guide.md](ai-guide.md): AI-assisted development workflow.

## Engineering

- [environment-setup.md](engineering/environment-setup.md): clean-machine bootstrap for AI agents, including international and mainland China download routes.
- [build-and-test.md](engineering/build-and-test.md): ESP-IDF build and validation.
- [firmware-layout.md](engineering/firmware-layout.md): default and user-defined partition layouts and merged-artifact validation.
- [coding-conventions.md](engineering/coding-conventions.md): source-code and resource conventions.

## Software architecture

- [passport-pixel-ui-design.md](passport-pixel-ui-design.md): current v2 visual design, 24 screen previews, approval cards, graphical battery, session routing and P1 desktop-pet synchronization.
- [passport-v2-protocol.md](passport-v2-protocol.md): negotiated session routing, approval receipts, and automatic companion transfer wire contract.
- [passport-service-architecture.md](passport-service-architecture.md): Passport Service boundaries, protocol, state model, and real NFC/IDE integration gates.
- [passport-service-status.md](passport-service-status.md): latest measured Passport flash, boot, validation, and known-boundary record.
- [passport-service-todo.md](passport-service-todo.md): authoritative continuation checklist for the Passport Service MVP.
- [physical-skills-mvp-design.md](physical-skills-mvp-design.md): Slice F Physical Skills MVP — Wear and Compose page design, protocol additions, and gesture additions.
- [ide-adapter-decision.md](ide-adapter-decision.md): Slice C decision record — Codex is the first local IDE adapter; Trae deferred.
- [nfc-path-decision.md](nfc-path-decision.md): Slice D decision record — phone-to-Bridge relay is the first NFC path; external reader deferred.
- [softap-fallback-decision.md](softap-fallback-decision.md): SoftAP + TCP fallback stays in-tree but off by default.

## Exploratory ideas

- [did-tibo-rest-idea.md](did-tibo-rest-idea.md): exploratory product idea for
  Tibo push reminders and Codex level controls on AI Passport; the current
  prototype is explicitly not the implementation baseline.

## CI

- [CI-validation.md](ci/CI-validation.md): pull-request and main-branch checks.
- [CI-build-and-release.md](ci/CI-build-and-release.md): tagged firmware builds and releases.
- [CI-sync-main.md](ci/CI-sync-main.md): upstream synchronization for forks.

## Release

- [publish-to-community.md](release/publish-to-community.md): publishing firmware to the AI Passport community market.
- [project-completion.md](release/project-completion.md): project completion flow — a menu of optional closing actions.
- [file-issues.md](release/file-issues.md): filing a suggestion as an upstream GitHub issue.
