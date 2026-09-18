<p align="right">
  <strong>English</strong> · <a href="passport-service-status.zh_CN.md">简体中文</a>
</p>

# Passport Service development status

Status date: 2026-09-18

Branch: `codex/passport-service-mvp`

Current phase: the Passport v2 implementation baseline is complete. The
firmware, Codex host route, approval receipts, graphical battery, session
picker and automatic PC companion transfer are integrated. The complete
repository gate passes and the v2 firmware has been flashed. A physical device
walkthrough is the next step.

This file records measured project state. It is not a release note.

## Implemented

- The existing pixel scene, 3.2-second card scan, four-card stack and
  release-to-stop voice flow remain in place.
- The device now advertises protocol 2 periodically. A changed 16-hex Bridge
  identity invalidates the route, pending approval and staged companion.
- `passport_v2_state` keeps a bounded three-row session page, stages complete
  catalogs before publishing them, rejects stale selection transactions and
  routes actions with the exact `bridge` / opaque `sid` / `epoch` tuple.
- Holding Up opens the session picker. Up/Down select a row and OK requests
  the switch. Approval and recording block switching; one recording keeps the
  route captured at start.
- Approval is rendered as an operation card with a details page. The device
  waits for `approval.receipt`; sending a decision alone is not displayed as
  execution success.
- The battery header is a four-segment pixel icon. Unknown SOC renders `?`;
  no charging mark is inferred from USB.
- The Codex adapter runs the compatibility MCP client and a real app-server
  client. `thread/list`, `thread/read`, `thread/resume` and `turn/start` provide
  exact existing-thread routing. A live read-only smoke returned one thread
  for this repository.
- App-server command/file approval requests and legacy MCP elicitations both
  map to Passport approvals, while preserving their native response enums.
- PC companion changes require no device interaction. The Bridge reads the
  selected Codex pet configuration, debounces changes for 500 ms, converts the
  verified v2 atlas to one 32x32 indexed frame, and transfers it only during
  idle traffic.
- Companion transfer uses 128-byte stop-and-wait chunks, offset ACKs and a
  SHA-256 commit. The inactive device slot is published only after complete
  validation; failures preserve the current companion.
- Trae remains an explicit protocol-1, read-only/synthetic-session fallback
  because its CLI does not prove exact routing to an existing conversation.

The normative protocol is
[`passport-v2-protocol.md`](passport-v2-protocol.md). The visual and interaction
contract is
[`passport-pixel-ui-design.md`](passport-pixel-ui-design.md).

## Resource and build evidence

- `s_v2`: 2,632 bytes of static RAM.
- `s_scene`: 760 bytes, including the active indexed companion used for draw.
- Application image: 1,596,944 bytes.
- Factory partition: 8,323,072 bytes; approximately 81% remains free.
- Fresh merged image: `build/FoloToy-AI-Passport-full.bin`, 1,662,480 bytes.
- SHA-256:
  `da49e579bcd3072e0225c626954d95857407e7955296e3adab68c5b7e491f61d`.
- Default partition layout remains NVS, PHY data and one factory application.

## Validation

Completed after the v2 implementation:

- `./tools/validate.sh --static`: PASS.
- Host C tests: PASS, including v2 catalog transactions, route isolation,
  approval receipts, companion offset/commit behavior and battery boundaries.
- Python tests: PASS under system Python, including 14 Codex adapter cases,
  four Bridge glue cases and four Pillow companion conversion/debounce cases.
- Real Codex app-server read-only smoke: PASS.
- Firmware build with ESP-IDF 5.5.3: PASS.
- Merged-image layout verification: PASS.
- Host rendering through `passport_scene_draw`: PASS for idle, scan, stacks
  and an indexed companion.

The ESP-IDF Python environment does not include optional Pillow, so its
companion tests report four skips. The same tests pass under the Bridge's
system Python environment where Pillow 10.4.0 is installed.

## Flash and device evidence

- Detected device: `/dev/cu.usbmodem2101`.
- The previous Slice F build was flashed and its release-to-stop voice flow
  was verified on-device.
- `./tools/validate.sh --preflash`: PASS immediately before the v2 flash.
- The validated bootloader, partition table and 1,596,944-byte application
  were written at 0x0, 0x8000 and 0x10000. All three device-side hashes passed
  and RTS hard reset completed. The NVS region at 0x9000 was not overwritten.
- After reset, the real USB Bridge received protocol-2 `device.hello` frames
  and returned `host.hello` plus the current empty route snapshot. Codex MCP
  0.139.0 and the exact-routing app-server both started successfully.

## Remaining verification

- Inspect the v2 battery, session picker, operation card and companion on the
  physical 240x320 display.
- Exercise a real app-server approval and confirm the device receipt states.
- Change the selected pet in the PC IDE and confirm automatic idle transfer.
  If the IDE exposes no selected-pet key, the Bridge correctly retains the
  existing device companion.
- Measure runtime free heap with four cards, recording and a companion transfer
  active. Static symbol sizes alone are not a peak-RAM measurement.
- Complete the remaining real NFC and external STT checks.

## Resume point

After the v2 flash, use the device walkthrough in
[`passport-service-todo.md`](passport-service-todo.md). Do not add device-side
pet selection or manual synchronization controls.
