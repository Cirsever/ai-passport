<p align="right">
  <strong>English</strong> · <a href="passport-service-status.zh_CN.md">简体中文</a>
</p>

# Passport Service Development Status

Status date: 2026-09-14
Branch: `codex/passport-service-mvp`
Current phase: Slice F Physical Skills MVP firmware in place and flashed to
device; real NFC, IDE adapter, and voice worker still pending. Chinese page
rendering awaits human visual confirmation.

This document records the latest measured development state. It is evidence for
resuming work, not a product-release note.

## Completed in this stage

- Passport boots directly into the Passport Service page for MVP hardware
  testing.
- The pure-C Service Core supports bounded protocol parsing, one-card admission,
  Goal request/confirmation state, task progress, approvals, Skill revision
  state, `task.event`, `tile.stack.state`, `context.composed`, `skill.updated`,
  the Wear/Compose page state, event ring, navigation, and event ack.
- `passport_ui_model` produces three pages (`WEAR.HOME`, `WEAR.TASK`,
  `COMPOSE.STACK`) with Chinese labels, a header (mode/link/battery), a
  variable-length body, a hint bar, and an approval overlay flag.
- `demo_passport_service` renders that model with a 16 px CJK-subset LVGL font
  (`ui_cn_16`). The approval overlay is a body-region panel that does not hide
  the header or hint.
- `tools/passport_bridge.py` accepts `!compose`, `!event`, `!skill`, `!task`,
  `!approval`, and `!help` mock commands so the host↔device flow can be
  exercised without a Tile Reader.
- The USB Serial/JTAG transport uses `@passport ` newline-delimited JSON
  frames.
- A real card-event boundary is exposed through
  `demo_passport_service_nfc_card(const char *card_id)`. The firmware does not
  manufacture a card event.

## Hardware and flash evidence

- The board was detected at `/dev/cu.usbmodem2101` (ESP32-C3, 8 MB flash).
- `idf.py build` produced `FoloToy-AI-Passport.bin` = 1,557,088 bytes. The
  factory partition has 8,323,072 bytes; free space is approximately 81%.
- `idf.py -p /dev/cu.usbmodem2101 flash` wrote the bootloader (offset 0x0,
  0x5220 bytes), the partition table (0x8000, 3,072 bytes), and the factory
  application (0x10000, 1,557,088 bytes / 879,539 compressed). Hash of data
  verified. NVS was not touched.
- The device hard-reset via RTS pin and produced live USB Serial/JTAG RX
  traffic (`I passport_usb: USB RX bytes=2`) once a host started writing.
- The bridge successfully pushed mock frames to the device (task.state,
  task.event, tile.stack.state, context.composed, approval.request), and the
  device emits its `device.hello` on transport start-up without gating on the
  unreliable USB Serial/JTAG DTR bit.

## Validation evidence

The final validation was run after the last firmware change:

- `./tools/validate.sh --static`: PASS (197 text files scanned, 11 host tests).
- `./tools/validate.sh --firmware`: PASS.
- Firmware layout: PASS (1,557,088 / 8,323,072 bytes in `factory` at 0x10000).
- Merged image: PASS (1,622,624 bytes at flash 0x0).
- `idf.py -p /dev/cu.usbmodem2101 flash`: PASS.

The device test covered flashing, boot, USB Serial/JTAG RX, and delivery of
mock protocol frames from the host bridge. Chinese-page rendering on the
physical 240×320 display has been visually confirmed for the disconnected
banner (`WEAR.DISCONNECTED`) — every glyph rendered, no missing squares —
after regenerating `ui_cn_16` to cover the newly added strings. The remaining
Wear/Compose checkpoints in `tools/acceptance_slice_f.py` are pending an
operator walkthrough.

## Known boundaries

- The current board documentation does not define an MCU-side NFC Reader API
  or reader pin assignment. A phone simulating an NFC card cannot directly
  notify this ESP32 without an external reader or a phone-to-Bridge relay.
- The Codex adapter is now decided and skeleton-implemented in
  `tools/codex_adapter.py`. Handshake with `codex mcp-server` was measured
  live on this workstation (Codex CLI 0.139.0). The bridge glue for wiring
  the adapter into the physical device path ships as
  `passport_bridge.py --codex[/--codex-cwd/--codex-model/...]`; feed each
  device-side `@passport ` frame through `CodexAdapter.handle` and forward
  every emitted Passport frame back through the existing `send_json`. Host
  tests: `tests/test_codex_adapter.py` (6) + `tests/test_bridge_codex_glue.py`
  (3). Outstanding pieces before Slice C is production-ready: (a) approval
  round-trip channel, currently stubbed with a `bridge.error`, awaits
  inventory of Codex's approval notification stream; (b) real audio path so
  `voice.capture.stop` carries a transcript (Slice D + P0-5). Manual smoke
  path: `tools/manual_codex_smoke.py`.
- The audio codec initializes successfully, but Goal-mode recording, audio
  framing, and Bridge delivery are not implemented in the Passport Service
  page. `WEAR.VOICE` is documented but not yet wired.
- The screen was initialized successfully, but a human still needs to visually
  confirm the Chinese-labelled Wear/Compose pages on the physical display.
- Liveness on the USB Serial/JTAG transport is inferred from protocol-frame
  idle time (`link_idle_ms`) rather than the DTR bit. The DTR-based
  `passport_transport_usb_connected()` was removed and no code path consults
  `usb_serial_jtag_is_connected()`, because raw-open Python bridges never
  assert DTR, which would pin the disconnected banner permanently.

## Resume point

Start from [`passport-service-todo.md`](passport-service-todo.md). Immediate
next steps: (1) human visual verification of the Chinese pages on device,
(2) NFC input path decision, (3) first IDE adapter, (4) voice worker.

