<p align="right">
  <a href="physical-skills-mvp-design.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# AI Passport Physical Skills MVP — Page Design

Status: MVP design, derived from `AI_Passport_Physical_Skills_MVP_Design_v0.1`.

For the current visual design and complete screen gallery, see
[Passport pixel companion v2](passport-pixel-ui-design.md). Its approval,
battery, session and P1 pet specifications supersede the corresponding roadmap
mockups here. The implemented-scene record below remains the previous baseline.

## Implemented pixel scene (2026-09)

This section supersedes the text-only home/stack mockups below. Other mockups
remain roadmap proposals, including mode lock, skill detail, reload toast and
the full-body disconnect page.

- A 240×320 pixel scene uses a blue status header, cream cards, hard shadows,
  a pixel robot, role colors and a reader dock. The header is 43 px; the body
  occupies y=47–263; the two-line button footer occupies y=268–316.
- `nfc.present` (`card_id` matching `[A-Za-z0-9_:.-]{1,47}`)
  is sent by the bridge **before** starting the IDE adapter. It is display-only:
  it does not enable goal mode, enqueue an action or fabricate a tile stack.
- The first observed card scans, then stays prominent for 3200 ms total.
  A changed multi-card stack animates for 1400 ms; entries stagger by 120 ms.
  The settled card/stack remains visible. Identical snapshots do not restart
  animation. Removal and re-addition re-arm it.
- `tile.stack.state` is authoritative for up to four overlapping cards:
  index 0 is the front/top card. Down cycles the highlighted tile; its role,
  identity and revision appear below the stack. Repeated NFC taps never count
  as additional tiles. Empty stacks clear the stack scene.
- Animation completion is not host success. Only `goal.mode.state` or
  `context.composed` confirms readiness. Pending, conflict and offline states
  remain distinguishable. Task progress comes from `task.state.progress`.
- Approval and voice overlays take priority; scene time pauses while covered
  or on the task detail page. Voice completion remains visible for 2 s.
- A single custom LVGL drawing object uses the existing 100 ms refresh timer.
  Scene state and rectangle geometry are pure C; there is no framebuffer,
  per-card LVGL object tree, extra animation task or runtime image download.

Physical NFC/Tile Reader integration is still external to this firmware.
The relay provides a single-card observation; multiple tiles require real
`tile.stack.state` input from the reader/host.

Geometry preview (idle, single, three and four cards; firmware rectangle
renderer, without LVGL labels):

```bash
cc -std=c11 -Imain tools/preview_passport_scene.c main/passport_scene.c \
    -o /tmp/preview_passport_scene
mkdir -p build
/tmp/preview_passport_scene > build/passport-scene.ppm
```

Host coverage in `tests/test_passport_scene.c` includes timing boundaries,
overlay pause, duplicate observations, conflict preservation, removal/re-add,
local-reader admission after removal, invalid IDs, overflow stacks and
rectangle bounds across every frame and selection. Bridge tests verify that
the observation reaches the wire before IDE dispatch. Device acceptance still
requires real reader events, screen inspection and concurrent audio capture.

Scope: page layout, page-level state transitions, protocol additions, and
button gestures for Wear Mode and Compose Mode on the current Passport
hardware baseline. Non-UI logic (host-side Skill Registry, hot-reload engine,
Tile Reader hardware, mechanical design) is out of scope and stays in the host
project.

This document assumes and does not restate:

- Hardware baseline: ESP32-C3 + ST7789P3 240×320 + ES8311 + CW2017 +
  three-button ADC ladder ([`AI_HARDWARE_DEVELOPMENT_GUIDE.md`](../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md)).
- Protocol envelope: `@passport ` newline-delimited JSON lines
  ([`passport-service-architecture.md §4`](passport-service-architecture.md#4-service-protocol)).
- Transport, Service Core, timing, and repository split rules from the same
  architecture document.

## 1. Scope and non-goals

In scope:

- Screen frame and page inventory for Wear + Compose.
- Mode switching between Wear and Compose.
- Button gestures per page.
- Protocol types the device consumes and emits.
- Voice, approval, and skill-hot-reload feedback surfaces on the device.

Non-goals (owned by host or by later slices):

- Skill Registry, semver/hash validation, or file watcher.
- PN532 / RC522 firmware, driver, or Tile Reader mechanical work.
- Codex/Trae adapter behavior beyond what the envelope already defines.

## 2. Modes and mode-switch rule

Two modes:

- `WEAR`: Passport is the companion display for a running Agent task.
- `COMPOSE`: Passport shows the current physical Skill stack and its resolved
  context.

Automatic rule:

- Tile stack empty → `WEAR`.
- Tile stack has one or more tiles → `COMPOSE`.

Manual override:

- `DOWN` long (≥ 2 s) toggles a `mode.lock` state. When locked, automatic mode
  switching is suppressed until the user releases the lock or the tile stack
  changes to an incompatible state (for example, entering an approval while
  locked to `COMPOSE` still shows the approval overlay).

The header always shows the effective mode; a padlock glyph appears when the
mode is user-locked.

## 3. Screen frame

Frame is fixed for every page so that peripherals (header, hint bar) are
consistent muscle memory:

```text
┌─────────────────────────────┐  0
│  header  24 px              │  status, mode, battery
├─────────────────────────────┤  24
│                             │
│  body  272 px               │
│                             │
├─────────────────────────────┤  296
│  hint bar  24 px            │  context-sensitive button legend
└─────────────────────────────┘  320
```

Layout rules:

- All LVGL access is inside `bsp_lvgl_lock()`.
- Header and hint bar are owned by a single page-frame widget; page bodies
  never redraw them directly. This keeps the mode / battery / hint invariants
  from being clobbered by page code.
- Overlays (approval, voice capture, toast) occupy only the body region unless
  explicitly noted; header and hint bar remain visible.
- On-screen labels are localized in the Chinese sibling document; the English
  document keeps English labels in mockups for readability. Protocol `type` /
  `state` values and hardware button constants (`UP` / `DOWN` / `OK`) stay in
  English on every surface.

## 4. Wear Mode pages

### 4.1 `WEAR.HOME`

Landing page whenever Passport is worn and the stack is empty.

```text
┌─────────────────────────────┐
│ WEAR  USB READY       BAT ▮ │
├─────────────────────────────┤
│                             │
│ RUNNING  42%                │
│ Refactor tracing            │
│                             │
│ EVENT  analyzer summary     │
│                             │
│ NFC   card-1                │
│ GOAL  CODEX / session-1     │
│                             │
├─────────────────────────────┤
│ UP task   DOWN compose  OK ▶│
└─────────────────────────────┘
```

Data sources: `task.state`, most recent `task.event` (see §7),
`goal.mode.state`, `nfc` identity from Service Core.

### 4.2 `WEAR.TASK`

Expanded task summary and rolling event log (last 5 events).

```text
┌─────────────────────────────┐
│ WEAR  TASK runtime-1  BAT ▮ │
├─────────────────────────────┤
│ RUNNING 42%                 │
│ Refactor tracing across     │
│ pkg/foo/bar/**              │
│                             │
│ 12:30 analyzer scan started │
│ 12:31 3 findings queued     │
│ 12:33 patch draft ready     │
│ 12:35 tests running…        │
│                             │
├─────────────────────────────┤
│ UP back   DOWN goal   OK ack│
└─────────────────────────────┘
```

`OK short` here acknowledges the top event (device emits `task.event.ack`);
this is separate from voice capture, which lives on `WEAR.HOME`.

### 4.3 `WEAR.APPROVAL` (overlay)

Triggered by `approval.request`; overlays the body region on any Wear page.

```text
┌─────────────────────────────┐
│ ! APPROVAL            BAT ▮ │
├─────────────────────────────┤
│ Apply 3 files changed       │
│ Risk  WRITE                 │
│                             │
│ Files                       │
│   src/net/http.c            │
│   src/net/http.h            │
│   src/util/log.c            │
│                             │
├─────────────────────────────┤
│ UP reject  DOWN detail  OK ✓│
└─────────────────────────────┘
```

The 60 s approval timing rule from architecture §4.2 applies. The overlay
never covers the header, so mode and battery stay visible while approving.

### 4.4 `WEAR.VOICE` (overlay)

Triggered by `OK` hold (≥ 300 ms) on `WEAR.HOME`. Released on `OK` up.

```text
┌─────────────────────────────┐
│ WEAR  LISTENING       BAT ▮ │
├─────────────────────────────┤
│                             │
│    ▮ ▮▮▮ ▮▮▮▮▮ ▮▮▮ ▮        │
│    ------- 0:04 --------    │
│                             │
│ "how far are we with the    │
│  refactor?"                 │
│                             │
├─────────────────────────────┤
│ UP cancel      hold OK talk │
└─────────────────────────────┘
```

Audio worker runs in a dedicated task; the LVGL task only redraws the level
meter based on peak values published by the worker.

### 4.5 `WEAR.DISCONNECTED`

Full-body state when the bridge link drops. Header still shows `HOST OFFLINE`.

```text
┌─────────────────────────────┐
│ WEAR  HOST OFFLINE    BAT ▮ │
├─────────────────────────────┤
│                             │
│ ⚠ Bridge lost               │
│ Last snapshot 12:30         │
│                             │
│ Reconnect: keep worn or     │
│ return to base              │
│                             │
├─────────────────────────────┤
│ UP retry    DOWN snapshot   │
└─────────────────────────────┘
```

Device never invents state. Snapshot view shows the last-known task/event/goal
values with a `stale since HH:MM` banner.

## 5. Compose Mode pages

### 5.1 `COMPOSE.STACK`

Landing page for Compose. Bottom-to-top order matches the physical stack.

```text
┌─────────────────────────────┐
│ COMPOSE  3 tiles      BAT ▮ │
├─────────────────────────────┤
│ top ─────                   │
│   ▶ [ REVIEW    v0.3.2 ]    │
│     [ PROJECT   aide ]      │
│     [ AIDE      v0.4.1 ]    │
│ base ─────                  │
│                             │
│ CONTEXT  aide + project     │
│          + review           │
│ STATUS   composed 640 ms    │
├─────────────────────────────┤
│ UP wear  DOWN cycle  OK open│
└─────────────────────────────┘
```

Selection indicator (`▶`) starts on the top tile and moves down with `DOWN
short`; wraps back to top after the last tile. `OK short` opens
`COMPOSE.SKILL` for the selected tile.

The composition status row shows the latest `context.composed` result or a
red `CONFLICT` if the resolver rejected the stack.

### 5.2 `COMPOSE.SKILL`

Detail page for the currently selected tile's skill.

```text
┌─────────────────────────────┐
│ COMPOSE skill REVIEW  BAT ▮ │
├─────────────────────────────┤
│ id        review            │
│ revision  0.3.2 · pinned    │
│ accepts   code, project     │
│ tools     git.diff          │
│           code.search       │
│ write_code  false           │
│ shell       confirm         │
│                             │
├─────────────────────────────┤
│ UP back  DOWN reload  OK pin│
└─────────────────────────────┘
```

Fields come from the host's `skill.updated` payload. The device does not
compute prompt content; it only mirrors the revision id and permission
summary.

### 5.3 `COMPOSE.RELOAD` (toast)

Transient (2 s) overlay when the host emits `skill.updated` for a skill in the
current stack.

```text
        ┌──────────────────────┐
        │ SKILL.UPDATED        │
        │ review 0.3.2 → 0.3.3 │
        │ session pinned       │
        │ OK adopt  UP keep    │
        └──────────────────────┘
```

If the user does not act, the toast disappears and the session keeps the
pinned revision, matching the design doc's "session pin unless user reloads"
rule.

## 6. Page transition diagram

```text
        approval.request                approval.decision
             │                                │
             ▼                                │
      [WEAR.APPROVAL] <────────────────────── │
             ▲                                │
             │                                │
[DISCONNECTED] ─ link up ─▶ [WEAR.HOME] ◀─ UP ─ [WEAR.TASK]
             ▲                 │  ▲
             │  link down      │  │ hold OK
             └─────────────────┤  ▼
                               │ [WEAR.VOICE]
        stack ≥ 1              │
             ▼                 │
      [COMPOSE.STACK] ◀── UP ──┘
             │  ▲
             │  │ UP
             ▼  │
      [COMPOSE.SKILL]
             │
             ▼ (transient)
      [COMPOSE.RELOAD toast]
```

`DOWN` long toggles the `mode.lock`; when locked, the two vertical
`stack ≥ 1` / `stack = 0` transitions above are suppressed.

## 7. Protocol extensions

All new messages share the existing `@passport ` line envelope from
[`passport-service-architecture.md §4`](passport-service-architecture.md#4-service-protocol).
Message-type additions:

Host → Passport:

```json
{"type":"task.event","task_id":"runtime-1","event_id":"e-9","summary":"analyzer scan started","ts":"12:30"}
{"type":"tile.stack.state","context_id":"ctx-14","stack":[{"tile_id":"aide","role":"agent","skill_id":"aide","revision":"0.4.1"},{"tile_id":"project.aide","role":"project","skill_id":"project","revision":"0.1.0"},{"tile_id":"review","role":"skill","skill_id":"review","revision":"0.3.2"}]}
{"type":"context.composed","context_id":"ctx-14","skills":["aide","project","review"],"duration_ms":640,"status":"ok"}
{"type":"skill.updated","skill_id":"review","revision":"0.3.3","previous":"0.3.2","status":"ready","summary":"Adjust review prompt"}
{"type":"voice.reply","request_id":"v-4","text":"about 42%, tests running"}
```

Passport → Host:

```json
{"type":"task.event.ack","event_id":"e-9"}
{"type":"skill.pin","skill_id":"review","revision":"0.3.2","source":"button"}
{"type":"skill.reload","skill_id":"review","source":"button"}
{"type":"voice.capture.start","request_id":"v-4","sample_rate":16000,"codec":"pcm16"}
{"type":"voice.capture.chunk","request_id":"v-4","seq":0,"payload_b64":"…"}
{"type":"voice.capture.stop","request_id":"v-4","duration_ms":4200,"reason":"release"}
```

Rules:

- Unknown or malformed messages are rejected without mutating valid state.
- `tile.stack.state` is authoritative: an empty `stack` means the physical
  stack is empty and the device leaves Compose. The device never invents a
  stack from a UID guess.
- `skill.updated` for a `skill_id` that is not in the current stack updates
  the Skill Registry state on the host side only; the device ignores it.
- Voice frames are bounded (default 20 ms @ 16 kHz PCM) and dropped if the
  transport is congested; the host is responsible for recovery.
- Adding a new `type` requires updating this file and the protocol section in
  architecture before implementation.

## 8. Button gesture additions

The following rows are added to
[`passport-service-architecture.md §7.1`](passport-service-architecture.md#71-button-gesture-registry).
The registry is the source of truth; this section only lists Physical Skills
MVP additions.

| Gesture              | Page / state         | Action                                     | Owner slice        |
| -------------------- | -------------------- | ------------------------------------------ | ------------------ |
| `OK` hold (≥ 300 ms) | `WEAR.HOME`          | Enter `WEAR.VOICE` and stream audio        | Service            |
| `OK` release         | `WEAR.VOICE`         | Stop capture, emit `voice.capture.stop`    | Service            |
| `UP` short           | `WEAR.VOICE`         | Cancel capture, no chunk delivered         | Service            |
| `UP` short           | `WEAR.HOME`          | Enter `WEAR.TASK`                          | Service            |
| `DOWN` short         | `WEAR.HOME`          | Enter Compose if stack ≥ 1                 | Physical Skills    |
| `OK` short           | `WEAR.TASK`          | Ack top event (`task.event.ack`)           | Service            |
| `DOWN` short         | `COMPOSE.STACK`      | Cycle selection down (wraps)               | Physical Skills    |
| `OK` short           | `COMPOSE.STACK`      | Open `COMPOSE.SKILL` for selected tile     | Physical Skills    |
| `UP` short           | `COMPOSE.STACK`      | Return to `WEAR.HOME`                      | Physical Skills    |
| `UP` short           | `COMPOSE.SKILL`      | Back to `COMPOSE.STACK`                    | Physical Skills    |
| `DOWN` short         | `COMPOSE.SKILL`      | Send `skill.reload`                        | Physical Skills    |
| `OK` short           | `COMPOSE.SKILL`      | Toggle pin, emit `skill.pin`               | Physical Skills    |
| `OK` short           | `COMPOSE.RELOAD`     | Adopt new revision (adopt=drop pin)        | Physical Skills    |
| `UP` short           | `COMPOSE.RELOAD`     | Keep current pin                           | Physical Skills    |
| `DOWN` long (≥ 2 s)  | Any                  | Toggle mode-lock                           | Service            |

Existing rows from architecture §7.1 (approval, notification, level) are
unchanged; conflicting page-scoped rows above win only inside their page.

## 9. Voice, sound, and haptic feedback

- Voice capture: 16 kHz PCM16 mono, 20 ms frames, worker-task driven, bounded
  ring buffer, dropped-frame counter reported on `voice.capture.stop`.
- Approval / notification / compose feedback tones share one small waveform
  bank in flash; audio playback is serialized so approval never overlaps
  voice reply.
- No vibration motor on the current baseline; feedback is limited to audio
  and screen flash. A screen-flash budget is 100 ms and does not interrupt
  the LVGL frame budget.

## 10. Acceptance

Physical Skills MVP is accepted when all of the following hold on the real
device, not on a fixture:

1. `WEAR.HOME` shows real task state, event, goal, and NFC identity within
   250 ms of the host message.
2. `OK` hold on `WEAR.HOME` produces one contiguous voice capture with
   correct start/stop framing and no LVGL-task starvation.
3. `WEAR.APPROVAL` overlays correctly, honors the 60 s timeout, and never
   loses the header or hint bar.
4. A real 3-tile stack lands in `tile.stack.state`, produces
   `context.composed`, and `COMPOSE.STACK` reflects the stack within 800 ms.
5. `skill.updated` for a tile in the current stack shows the reload toast and
   is dismissable by the two documented gestures.
6. `DOWN` long locks the mode; stack changes do not switch modes until
   unlocked.
7. Bridge disconnect drops to `WEAR.DISCONNECTED` with a stale banner and no
   invented state.

Build, host tests, and device tests are reported separately as the repository
convention requires.

## 11. Open decisions

- Concrete Skill Registry contract on the host side (fields, permission
  vocabulary). This document only pins the fields the device renders.
- Voice reply delivery: synthesized on host (audio stream) vs text-only
  short reply. Current mock uses text; audio path lands with Slice D.
- Whether `mode.lock` should be persisted across reboot. Default in MVP: no.
- Selection navigation on `COMPOSE.STACK` when stack size grows beyond four
  tiles; the current three-tile MVP does not require scrolling.

## 12. Where this fits in the slice roadmap

This design is the concrete UI/protocol content of **Slice F: Physical Skills
MVP** in [`passport-service-architecture.md §8`](passport-service-architecture.md#8-implementation-slices).
Slice F depends on:

- Slice A/B (Service Core + real transport) — required.
- Slice D audio worker — required for `WEAR.VOICE`.
- Slice C IDE adapter — required for `task.event` and `voice.reply` beyond a
  local mock.

Tile hardware (PN532 or fallback identification) is intentionally left
outside Slice F and follows the design-doc "Phase 0 physical characterisation
first, mounting later" rule.
