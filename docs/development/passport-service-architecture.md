<p align="right">
  <strong>English</strong> · <a href="passport-service-architecture.zh_CN.md">简体中文</a>
</p>

# Passport Service Architecture

## 1. Product boundary

Passport is a wearable companion for a local Agent running on a Mac or PC. It
does not execute Codex, Trae, tools, permissions, or memory. The device owns
physical input, audio capture, display, bounded task state, and a transport to
the local bridge. The bridge owns IDE selection, Goal-mode activation, voice
delivery, and the adapter for each IDE.

The intended flow is:

```text
phone emulates one NFC card
        │
        ├─ NFC reader attached to Passport, or an explicit phone-to-bridge relay
        ▼
Passport card event
        ▼
local bridge selects Trae/Codex adapter and enables Goal mode
        ▲                                      │
Passport button starts local audio capture ────┘
        │
        ▼
bridge sends the utterance to the active IDE session
        │
        ▼
IDE progress / approval / completion → Passport display
```

The current board documentation is an important hardware gate: the exposed
NFC component is a passive NTAG213 and the ESP32 has no documented MCU-side NFC
reader API or pin assignment. A phone can read or write the passive tag, but a
phone-emulated tag cannot notify the ESP32 through that component. The direct
tap path therefore requires an external reader or a phone relay.

## 2. Current MVP scope

- Keep one connected host session over USB Serial/JTAG with an `@passport ` line
  envelope; retain SoftAP/TCP for development fallback.
- Accept bounded task, approval, Goal-session, and Skill-revision updates from
  the local bridge.
- Display task progress, approval text, card identity, and Goal state.
- Emit approval decisions from the physical buttons.
- Admit exactly one normalized card identity per service session through
  `passport_service_load_goal_card()`.
- Emit one `goal.mode.request` for the accepted card and wait for the bridge to
  acknowledge the active IDE session.
- Keep the firmware free of Codex/Trae-specific shortcuts and credentials.

The current firmware does not auto-load a card, inject offline task progress,
or acknowledge Goal mode. The host bridge is now a protocol client only; a real
IDE adapter must provide those events.

## 3. System boundaries

```text
Mac / PC
  Trae adapter       Codex adapter       task/progress source
       \                 |                 /
                 passport bridge
       │ USB Serial/JTAG / BLE / TCP
       ▼
Passport service core
  bounded protocol state
  one-card admission
  Goal-mode request/ack state
  approval actions
       ├── LVGL UI
       ├── button event adapter
       ├── audio worker
       └── NFC event adapter
             ├── external NFC reader (required for direct tap)
             └── phone relay event (alternative)
```

The pure C service core has no LVGL, NimBLE, socket, or NFC dependency. The
device adapter converts a real card event into the core call, consumes the
resulting action, and sends the request over the active transport.

The reference voice implementation uses BLE notifications for compressed
16-kHz audio and raw button gestures, while the desktop bridge maps those
events to application-specific shortcuts. That division is retained here:
Passport should report gestures and audio; the bridge should decide whether
the active target is Trae or Codex.

## 4. Service protocol

The [v2 wire contract](passport-v2-protocol.md) extends this envelope with
negotiated session routing, approval receipts and automatic companion assets.
Protocol-1 frames below remain the single-session compatibility path.

USB uses one UTF-8 JSON object per line with an `@passport ` prefix. Boot logs
without the prefix are ignored by the bridge. Example messages are:

Host to Passport:

The NFC relay sends `{"type":"nfc.present","card_id":"card-1"}` before IDE
dispatch so the display can animate immediately. `card_id` must match
`[A-Za-z0-9_:.-]{1,47}`. This observation never authorizes a Goal session,
binds its identity, or emits an action. Duplicate observations do not restart
the scene. Multi-card layout comes only from `tile.stack.state`; readiness
comes from `context.composed` / `goal.mode.state`, never an animation timer.

```json
{"type":"task.state","task_id":"runtime-1","state":"running","progress":42,"summary":"Refactoring tracing"}
{"type":"goal.mode.state","mode":"goal","state":"enabled","card_id":"card-1","ide":"codex","session_id":"codex-session-1"}
{"type":"approval.request","request_id":"request-7","summary":"Apply 3 files changed"}
```

Passport to host:

```json
{"type":"device.hello","protocol":1,"device":"FoloPassport"}
{"type":"goal.mode.request","mode":"goal","card_id":"card-1"}
{"type":"approval.decision","request_id":"request-7","decision":"approve"}
```

Skill revision (host to Passport) shares the same envelope:

```json
{"type":"skill.revision","skill_id":"codex.review","revision":"r7","summary":"Review firmware diff"}
```

Notification messages for the DidTiboRest slice extend the same namespace and
are defined in [`did-tibo-rest-idea.md`](did-tibo-rest-idea.md):
`notify.push`, `notify.mute`, `notify.level`, `notify.state`. New `type` names
introduced by any future slice must land in this file first so both slices
share one envelope.

Protocol rules:

- Unknown or malformed messages are rejected without mutating valid state.
- Strings are bounded before copying into device memory.
- Only the bridge/IDE adapter may acknowledge an active Goal session.
- A second card identity is rejected while the first identity is active.
- The device never receives or stores IDE credentials.

### 4.1 Transport abstraction

Service Core depends on a narrow line-transport interface, not on USB, BLE, or
TCP directly:

```c
typedef struct {
    int  (*send_line)(const char *utf8_line);
    void (*on_line)(const char *utf8_line, void *ctx);
} passport_transport_t;
```

USB Serial/JTAG is the current implementation. Swapping to BLE notifications or
TCP must not require changes inside Service Core; Slice A acceptance covers a
mock transport that exercises the same host-test matrix.

### 4.2 Timing and fallback

- `goal.mode.request` waits for `goal.mode.state=enabled` for at most 30 s.
  On timeout Passport returns to `NO_CARD`, emits `goal.mode.state=timeout` on
  the local page, and requires a new card event to retry.
- `approval.request` without a matching `approval.decision` inside 60 s reverts
  the button hint to the previous state but keeps the approval banner until the
  bridge cancels or resolves it.
- Reconnect keeps the last accepted card only if the bridge replays
  `goal.mode.state=enabled`; otherwise the device drops to `NO_CARD`.

## 5. Device state model

```text
NO_CARD
  └─ CARD_REQUESTED
       └─ GOAL_ENABLED
            ├─ IDLE
            ├─ RUNNING
            ├─ WAITING_APPROVAL
            ├─ DONE
            └─ ERROR
```

The card event is an input boundary, not a simulated timer:

```text
real reader / phone relay
        → passport_service_load_goal_card(card_id)
        → goal.mode.request
        → bridge selects and starts the IDE adapter
        → goal.mode.state=enabled
```

Button gestures and audio follow the same host-owned rule:

```text
button gesture → device starts capture immediately
               → audio/event transport → bridge
               → active IDE adapter
```

## 6. Firmware rules

- Button callbacks remain non-blocking; audio capture runs in a worker task.
- LVGL access is performed under `bsp_lvgl_lock()`.
- The transport adapter never invents a card, task, approval, or IDE state.
- USB is the current lab transport. BLE is the intended wireless audio/event
  transport after the protocol boundary is stable.
- NFC reader code must live in a BSP/event adapter and may only use pins and
  buses confirmed by the hardware documents or measured on the board.

## 7. Passport page layout

The page displays facts that the device can currently confirm. It keeps the phone
card, IDE selection, and host link as separate pieces of state. From top to bottom:

```text
┌────────────────────────────┐
│ HOST WAIT / USB READY  BAT │  host link and battery
├────────────────────────────┤
│ RUNNING 42%                │  task state and progress
│ Refactoring tracing        │  one-line task summary
├────────────────────────────┤
│ NOTIFY: Tibo push (2)      │  optional event row, hidden when empty
├────────────────────────────┤
│ NFC: card-1                │  last accepted card identity
│ GOAL: CODEX / session-1   │  Goal state, IDE, and session
├────────────────────────────┤
│ GOAL ACTIVE  DOWN info    │  current button hint
└────────────────────────────┘
```

Display rules:

- Without a card, show `NFC: NO CARD` and `Tap NFC card to start`; do not send a
  Goal request.
- After the card is accepted but before the Bridge confirms an IDE, show
  `GOAL: WAIT IDE` and `Waiting for IDE...`.
- After confirmation, show `GOAL: CODEX / <session>` or the corresponding Trae
  session and enter task-state display.
- While approval is pending, show `APPROVAL: <summary>` and change the bottom
  hint to `OK approve  UP reject`, so an ordinary button action is not mistaken
  for approval.
- The event row is reserved for the DidTiboRest slice. When empty it collapses;
  when populated it never overlays the approval banner or task summary.
- `DOWN` opens a second page with Skill/Goal details. It only changes the local
  page and does not change service state.

The page is connected to the Passport Service Core, host-link state, battery
reading, and the real card-event boundary. The NFC Reader/phone relay, IDE
adapter, and audio transport remain separate slices and are not faked in the UI.

## 7.1 Button gesture registry

All gestures across every slice register here. A new slice must not claim a
gesture without editing this table first; the goal is to keep DidTiboRest,
approval, and page navigation from colliding on the same three-button ladder.

| Gesture              | State                    | Action                                 | Owner slice   |
| -------------------- | ------------------------ | -------------------------------------- | ------------- |
| `UP` short           | task view                | (reserved)                             | Service       |
| `UP` short           | approval pending         | Reject approval                        | Service       |
| `DOWN` short         | task view                | Toggle Skill/Goal detail page          | Service       |
| `OK` short           | task view                | Start / stop voice capture             | Service       |
| `OK` short           | approval pending         | Approve                                | Service       |
| `OK` short           | notification pending     | Mute the current notification event    | DidTiboRest   |
| `OK` long (>= 1 s)   | task view                | Cycle Codex level                      | DidTiboRest   |
| `UP` long (>= 1 s)   | any                      | (reserved for future slice)            | —             |
| `DOWN` long (>= 1 s) | any                      | (reserved for future slice)            | —             |

Notes:

- Notification and approval never occupy the same button state at the same
  time. If both are pending, approval wins the `OK` short gesture and the
  notification stays muted until the approval resolves.
- Slices must not overload `UP short` outside the approval state; that gesture
  is reserved so future navigation can extend without breaking existing muscle
  memory.
- Slice F (Physical Skills MVP) adds page-scoped gesture rows for `WEAR` and
  `COMPOSE` pages; those rows live in
  [`physical-skills-mvp-design.md §8`](physical-skills-mvp-design.md#8-button-gesture-additions)
  and only apply inside their pages.

## 8. Implementation slices

### Slice A: service core

Pure C state transitions, bounded parsing, one-card admission, Goal
acknowledgement, approvals, and host tests.

### Slice B: real transport adapter

USB Serial/JTAG framing and a host client that prints and sends protocol lines.
No automatic fixture or `--demo` mode is part of the device path.

### Slice C: local IDE bridge

Define an adapter contract for selected IDE, session identity, Goal activation,
task progress, approval, and voice delivery. Implement the first adapter only
after its local control surface is confirmed; do not guess undocumented Trae or
Codex APIs.

### Slice D: NFC and voice hardware

Choose the exact NFC reader or phone relay. Then add the reader event adapter
and the BLE/USB audio path. The reader model, bus, wiring, and power budget
must be recorded before BSP changes.

Voice defaults (subject to measurement): BLE notifications carrying 20 ms
16 kHz mono frames, Opus or G.711 depending on host CPU budget. USB Serial/JTAG
keeps raw PCM as the fallback for lab debugging.

#### NFC path decision matrix

| Option               | Extra hardware | BSP change      | User action                | First-run cost |
| -------------------- | -------------- | --------------- | -------------------------- | -------------- |
| External NFC reader  | PN532 / RC522  | Bus + pins + PSU| Physical tap on Passport   | High           |
| Phone-to-Bridge relay| None           | None            | Phone reads NFC → Bridge   | Low            |

The decision is expected in the same PR that starts Slice D. Neither option is
mandated by this document; both are legitimate once the facts are recorded.

### Slice E: notification companion (DidTiboRest)

Reuse Service Core, transport, page layout, and gesture registry. Add the
`notify.*` message set defined in [`did-tibo-rest-idea.md`](did-tibo-rest-idea.md),
the audio playback path introduced by Slice D, and the notification row above.
Slice E is blocked on Slice D audio delivery; it must not ship independent of
Passport Service.

### Slice F: Physical Skills MVP

Reuse Service Core, transport, and the button gesture registry. Add the
`WEAR` / `COMPOSE` page family, `task.event*`, `tile.stack.state`,
`context.composed`, `skill.updated`, `skill.pin`, `skill.reload`, and
`voice.capture.*` messages. Full UI, page transitions, and protocol details
live in [`physical-skills-mvp-design.md`](physical-skills-mvp-design.md).
Slice F depends on Slice A/B for transport, on Slice D for the voice worker,
and on Slice C for real task events; a local mock is acceptable while Slice C
is pending. Tile hardware identification (PN532 or fallback) is intentionally
outside this slice.

## 9. Acceptance and current blockers

The service gate is passed when a real input event, not a fixture, produces one
Goal request; the bridge acknowledges the selected session; task progress and
approval updates render; and button actions are emitted once.

The remaining blockers are external to the current state machine:

1. The current board does not document an MCU-connected NFC reader.
2. Trae and Codex adapter control surfaces have not yet been selected.
3. Wireless audio/event transport is not yet integrated into this firmware.

These are reported separately from host tests and firmware builds. A successful
build is not hardware validation.

## 10. Repository split policy

This repository is the single firmware baseline. New slices default to living
under `main/` and reusing the envelope, Service Core, transport, page layout,
and gesture registry defined above. A slice is only split into its own git
repository when at least one of the following triggers holds; documentation
alone is not a trigger.

### Firmware-side split triggers

A slice moves out of this repository as a separate firmware only if any of
these is measured and recorded:

1. The slice requires hardware that this baseline cannot accept, for example a
   different audio codec, a different display controller, a different flash
   size, or a partition table that conflicts with the baseline NVS/PHY/factory
   layout.
2. The slice has an independent release cadence that repeatedly misses this
   repository's validation gate, and forcing shared releases blocks either
   side for more than one cycle.
3. A third comparable slice appears and both new slices need a shared
   abstraction that Passport Service intentionally does not carry.

Until then, a slice stays here even if it owns a large feature surface. Being
"a different product idea" is not enough.

### Host-side split policy

Host bridges and IDE/service adapters (Codex, Trae, Tibo, and future ones) are
expected to live outside this firmware repository. They may share one
`*-passport-bridge` monorepo or split per adapter, but they are not owned by
this repository. The only contract they consume from here is the protocol
envelope in section 4 and the message-type list; that contract is versioned in
this repository and vendored downstream.

### Before splitting

Any proposed split records the following in the split PR:

- Which trigger above applies, with the measured evidence.
- The frozen envelope version and the message-type list at the moment of the
  split.
- The validation gate the new repository will run (`validate.sh --static`,
  host tests, firmware build, device tests) so that hardware evidence is not
  lost across the boundary.
- Whether any partition, BSP pin, or power decision changes as a result. If
  yes, it must land in this repository first.

The DidTiboRest slice does not currently satisfy any firmware-side trigger.
Its Slice E stays in this repository; its host bridge belongs to the host-side
policy above.
