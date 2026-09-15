<p align="right">
  <strong>English</strong> · <a href="passport-service-todo.zh_CN.md">简体中文</a>
</p>

# Passport Service TODO

This is the authoritative continuation list for the Passport Service MVP.
Completed items are kept here so the next session can distinguish verified work
from planned work.

Current phase: **Passport firmware/service foundation complete**  
Last evidence: [`passport-service-status.md`](passport-service-status.md)

## P0 — must decide before end-to-end MVP

- [x] Boot Passport directly into the Passport Service page.
- [x] Implement the pure-C Service Core and host tests.
- [x] Implement USB Serial/JTAG framing and the standard-library Bridge.
- [x] Remove automatic mock card, mock task, and automatic Goal behavior.
- [x] Add the real card-event boundary and one-card admission rule.
- [x] Render host link, battery, task, NFC, Goal, approval, and Skill information.
- [x] Build, flash, and boot-test the current firmware on the connected Passport.
- [x] Choose the NFC input path — phone-to-Bridge relay is the first
  implementation; external NFC Reader is documented as a follow-up. See
  [`nfc-path-decision.md`](nfc-path-decision.md).
  - [x] Relay endpoint wired in `tools/passport_bridge.py` (`--nfc-relay-port`
    / `--nfc-relay-host`); host tests in `tests/test_nfc_relay.py`. Phone
    picks between the three relay flavours documented in the decision doc.
  - [ ] Follow-up: external NFC Reader connected to Passport, with model,
    bus, pins, and power budget recorded from documentation or measurement.
    Only revisit under the triggers listed in the decision doc.
- [ ] Test one real phone/card event end to end. Needs operator: pair the
  chosen relay flavour with a real phone tap; acceptance is one
  `goal.mode.request` per unique UID, second UID rejected on the wire.
- [x] Select the first local IDE adapter: Codex (see
  [`ide-adapter-decision.md`](ide-adapter-decision.md)). Trae is deferred
  until it exposes a scriptable local control surface.
- [x] Implement the adapter contract for `goal.mode.state`, task progress,
  and session identity via `tools/codex_adapter.py` (MCP `tools/call codex`
  + `codex-reply`, host tests in `tests/test_codex_adapter.py`). Approval
  round-trip and physical end-to-end wiring remain follow-ups; see
  `docs/development/passport-service-status.md` for the outstanding pieces.
- [x] Wire the adapter into the physical device path via
  `passport_bridge.py --codex[/--codex-cwd/--codex-model/...]`; every
  device-side `@passport ` frame flows through `CodexAdapter.handle` and
  emitted frames go back over the existing `send_json`. Glue tests in
  `tests/test_bridge_codex_glue.py`.
- [x] Bounded Goal-mode voice stub: OK long-press on the device emits a
  paired `voice.capture.start` / `voice.capture.stop` frame that the Codex
  adapter treats as one utterance. Real audio worker + STT are deferred to
  Slice D + P0-5 (see `physical-skills-mvp-design.md`).

## P1 — hardware and interaction verification

- [ ] Visually inspect the flashed page on the physical 240x320 display: no
  clipping, readable status, page switch, and approval hint. **Needs
  operator** — the acceptance script is `tools/acceptance_slice_f.py`.
- [ ] Exercise the physical `UP`, `DOWN`, and `OK` paths with a real host
  connection; verify each action is emitted once. **Needs operator**.
- [ ] Send real `task.state`, `goal.mode.state`, and `approval.request` messages
  from the Bridge and verify state transitions on the device. **Needs
  operator** — covered offline by the C integration test in
  `tests/test_passport_service_integration.c`; the hardware trip stays as a
  human check.
- [x] TCP/SoftAP fallback resolved: keep the code, do not enable by default,
  do not touch PC Wi-Fi. See
  [`softap-fallback-decision.md`](softap-fallback-decision.md).
- [x] Integration test covering card admission, Goal confirmation, task
  progress, approval, event ack, skill reload, and disconnect timing:
  `tests/test_passport_service_integration.c`.

## P2 — hardening and delivery

- [x] Bounded error/status feedback for rejected host messages:
  `demo_passport_service.c` now emits a `protocol.reject` frame with a
  truncated echo of the offending line when the service core refuses to
  apply it.
- [ ] Record the selected NFC reader/relay wiring and runtime power behavior.
  **Needs operator** once a real relay flavour ships against a phone.
- [ ] Record the selected IDE adapter's compatibility and failure recovery.
  **Needs operator** once a real Codex session has been exercised through
  the bridge on device.
- [x] Run the complete validation gate after each transport or audio change:
  enforced structurally — `./tools/validate.sh` is the only entry point and
  it runs static + firmware together; individual devs may still invoke
  `--static` / `--firmware` for iteration.
- [x] Keep device tests separate from build results in every delivery report:
  standardised in `AGENTS.md` (`Build / Host tests / Device tests /
  Unverified` four-line format) and referenced from
  `docs/development/passport-service-status.md`.

## Slice F — Physical Skills MVP

Design doc: [`physical-skills-mvp-design.md`](physical-skills-mvp-design.md).

- [x] Extend `passport_service` with `tile.stack.state`, `context.composed`,
  `task.event`, and `skill.updated` parsing; add page/stack state and event
  ring buffer.
- [x] Rebuild `passport_ui_model` around Wear-home / Wear-task / Compose-stack
  pages with Chinese labels and dedicated header / body-rows / hint fields.
  Host tests lock the new contract.
- [x] Rewire `demo_passport_service` to a header + body-rows + hint frame with
  a hidden approval overlay; navigation and event ack use the new service
  helpers (`passport_service_navigate`, `passport_service_ack_top_event`).
- [x] Add `!compose` / `!event` / `!skill` / `!task` / `!approval` manual mock
  commands to `tools/passport_bridge.py`.
- [x] Package a CJK subset LVGL font (approx. 40–60 glyphs) for the labels
  emitted by `passport_ui_model` and switch `demo_passport_service` to it.
  Regeneration workflow lives in [`main/fonts/README.md`](../../main/fonts/README.md)
  (`tools/gen_cjk_font.sh` + `tools/collect_ui_glyphs.py`).
- [ ] Physical device verification: flash, walk through Wear-home ↔ Wear-task
  ↔ Compose-stack with the mock CLI, confirm approval overlay, event log,
  and Compose selection all render Chinese labels without missing glyphs.
  **Needs operator** — script: `tools/acceptance_slice_f.py`.
- [ ] Voice worker (`voice.capture.*`) with a real `WEAR.VOICE` overlay.
  Stub is live (see `send_voice_capture_burst` above); a real audio worker
  is still Slice D scope.
- [x] `WEAR.APPROVAL` 60 s timeout and `WEAR.DISCONNECTED` stale banner:
  `passport_service_tick()` ages the pending approval / link and the demo
  layer paints a full-body banner plus a retry/snapshot hint once the link
  has been idle past 3 s. Chinese strings live in `passport_ui_model.c`.

## Resume order

1. Operator visual verification of the Chinese pages
   (`tools/acceptance_slice_f.py`).
2. Real phone × NFC relay end-to-end (needs a chosen relay flavour).
3. Real Codex session run through the bridge (`--codex`), capture the
   approval notification stream and replace the stub in
   `tools/codex_adapter.py::_forward_approval_decision`.
4. Slice D audio worker (`voice.capture.*` real payloads and `WEAR.VOICE`
   overlay).
5. Update this checklist and
   [`passport-service-status.md`](passport-service-status.md) with the
   measured results.
