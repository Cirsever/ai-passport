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
- [x] Pick the first local IDE adapter: Codex (decision recorded in
  [`ide-adapter-decision.md`](ide-adapter-decision.md)). A minimal
  fire-and-forget Trae adapter (`tools/trae_adapter.py`, `passport_bridge.py
  --trae`) also ships; it makes no bidirectional / approval / session
  persistence guarantees and covers only the "tap card → open Trae Chat
  window → append utterance" loop. Production path stays on Codex.
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
- [x] Codex approval channel promoted from stub to real bridge:
  `CodexMcpClient` now distinguishes server-initiated JSON-RPC requests
  (`elicitation/create`, used by Codex for `exec_approval_request` and
  `apply_patch_approval_request`); `CodexAdapter.drain_pending_approvals()`
  translates each into a Passport `approval.request` frame pushed to the
  device; the operator's `approval.decision` (`approve` / `reject`) is
  routed back via `respond_to_server_request` and mapped to Codex's
  `ReviewDecision` (`approved` / `denied`). `passport_bridge.py` drains
  approvals every tick on both the USB and TCP loops. Coverage in
  `tests/test_codex_adapter.py::ApprovalRoundTripsThroughElicitation`
  locks four scenarios (positive, negative, unknown request_id, unsupported
  server method).
- [x] Bounded Goal-mode voice stub: OK long-press on the device emits a
  paired `voice.capture.start` / `voice.capture.stop` frame that the Codex
  adapter treats as one utterance. Slice D has since replaced this stub with
  the real audio worker and the host STT integration boundary described
  below (see `physical-skills-mvp-design.md`).

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
- [x] Preflash gate `./tools/validate.sh --preflash` required before every
  physical flash: runs the complete gate, verifies
  `build/FoloToy-AI-Passport-full.bin` is a fresh artifact of this run,
  warns on stale root-level app binaries, and prints an exact `esptool.py`
  command line derived from the firmware's `flasher_args.json`. Codified as
  a hard rule in `AGENTS.md` and `AGENTS.zh_CN.md`.
- [x] Regression static asserts for the three Slice F Major bugs + threshold
  constant collection + auto-scanned CJK coverage whitelist, all in
  `tools/validate.sh check_regression_asserts`.
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
- [x] Voice worker (`voice.capture.*`) with a real `WEAR.VOICE` overlay:
  Slice D MVP landed. `main/passport_voice_vad.[ch]` is a pure-C VAD (peak
  amplitude + silence hangover + hard duration cap; host tests
  `tests/test_passport_voice_vad.c` cover 7 cases).
  `main/passport_voice_worker.[ch]` is a dedicated FreeRTOS task: OK
  long-press starts one utterance, each 10 ms of PCM16 is base64-framed
  as `voice.capture.audio`, then releasing OK stops immediately; the
  800 ms silence timeout and hard duration cap remain fallbacks. The stop
  behavior is device-verified: two release-driven captures ended with
  `reason:"manual"` at 420 ms / 42 chunks and 3,180 ms / 318 chunks.
  The stop
  frame carries a placeholder
  `text` (`[voice N chunks Xms peak=Y]`) so Codex/Trae adapters have
  something concrete to forward until real STT lands. UI: the
  demo_passport_service hint line becomes an ASCII meter with an elapsed
  seconds counter while active (Chinese "recording" prefix rendered by
  the existing CJK subset); the meter is ASCII-only so the subset does
  not grow with each level change. After capture, "recording stopped" remains
  visible for two seconds and then the original navigation hint returns.
  This feedback window is a pure-C state machine covered by host tests and
  confirmed on the physical device.
  Font subset went 126 to 128 glyphs (two new glyphs for the recording label).
- [x] Host-side STT integration boundary: `tools/passport_stt.py` assembles
  bounded, validated `voice.capture.audio` frames into a temporary PCM16 WAV
  and runs an operator-supplied `--stt-command` before dispatching the stop
  frame to either Codex or Trae. The command must contain `{wav}` and print
  plain transcript text to stdout. Failure preserves the device diagnostic
  text, and the temporary WAV is deleted. Offline coverage lives in
  `tests/test_passport_stt.py`. A concrete STT engine/model remains an
  operator dependency; none is installed on the current workstation.
- [x] `WEAR.APPROVAL` 60 s timeout and `WEAR.DISCONNECTED` stale banner:
  `passport_service_tick()` ages the pending approval / link and the demo
  layer paints a full-body banner plus a retry/snapshot hint once the link
  has been idle past 3 s. Chinese strings live in `passport_ui_model.c`.

## Resume order

1. Operator visual verification of the Chinese pages
   (`tools/acceptance_slice_f.py`).
2. Real phone × NFC relay end-to-end (needs a chosen relay flavour).
3. Real Codex session run through the bridge (`--codex`), exercising the
   `elicitation/create` approval round-trip. Blocker on this workstation:
   Codex CLI 0.139.0 rejects both the config default `gpt-5.6-luna`
   ("model requires a newer version of Codex") and `gpt-5` ("not supported
   when using Codex with a ChatGPT account"). Upgrade the CLI or switch to
   an API-key account before running the real session.
4. Configure and measure a concrete STT engine/model through
   `--stt-command`, for example a local whisper.cpp command whose argv
   includes `{wav}` and whose stdout is plain transcript text. The Bridge
   assembly and fallback path are implemented; this workstation currently
   has neither a Whisper executable nor a model.
5. Update this checklist and
   [`passport-service-status.md`](passport-service-status.md) with the
   measured results.
