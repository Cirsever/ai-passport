**English** · [简体中文](/docs/README.zh_CN.md)

<h1 align="center">AI Passport</h1>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="../assets/images/logo-wordmark-dark.png">
    <img src="../assets/images/logo-wordmark.png" alt="AI Passport wordmark" width="180">
  </picture>
</p>

<p align="center">
  <strong>Bring IDE context into the physical world.</strong><br>
  One card selects capability, one screen confirms scope, and one companion keeps the state visible.
</p>

<p align="center">
  <a href="#the-concept">Concept</a> ·
  <a href="#how-one-card-works">User flow</a> ·
  <a href="#current-implementation">Implementation</a> ·
  <a href="#start-developing">Development</a>
</p>

---

## The concept

AI Passport is a physical interface for local IDE workflows. It does not shrink a
chat window onto a small display. It turns working context into state that can be
touched, selected, and confirmed:

- **Writable NFC Skill cards** select an IDE, mode, and bounded Skill reference.
  They do not contain credentials, local paths, prompts, or executable commands.
- **Passport Host Service** runs on the local PC. It recognizes cards, validates
  records, discovers local capabilities, selects IDE sessions, and orchestrates work.
- **Passport Device Service Core** runs on the ESP32-C3. It displays resolved state
  and reports physical input; it never installs or executes a Skill.
- **Pixel companions** express task progress, approval scope, offline retention,
  multi-session state, battery, and automatic PC-side companion synchronization.

The card is the entry point, the Host Service is the trust boundary, and the
device is the feedback and confirmation layer. Every card input goes through the
same parser and policy path before an IDE can receive an execution request.

<p align="center">
  <img src="../assets/images/passport-ui-v2/01-overview.zh.png" alt="AI Passport pixel UI overview showing idle companion, NFC scan, ready card, card stack, and task progress" width="100%">
</p>

<p align="center"><sub>240 × 320 pixel UI overview: tap a card, wake the companion, read the card, and confirm the task.</sub></p>

## How one card works

The current V1 record is an NDEF Text payload that can be written with the free
edition of NFC Tools:

```text
aip:1;i=codex;m=agent;s=review
```

The card does not carry the Skill implementation. Host Service resolves `i`, `m`,
and `s` against an installed IDE adapter, supported mode, and local Skill. Missing,
duplicate, oversized, or unsupported fields become explicit bounded error states.

```text
Writable Type 2 card
    ↓
Passport Host Service discovers local IDEs and Skills
    ↓
Configurator generates the canonical aip:1 text
    ↓
Android NFC Tools Free writes and reads the text back
    ↓
Host Service validates, resolves capabilities, and selects a session
    ↓
Passport displays progress and approval scope; physical buttons confirm decisions
```

<p align="center">
  <img src="../assets/images/nfc-fan-demo/nfc-fan-card-concept-v1.png" alt="Physical AI Passport NFC card concept showing cards, NFC coil, magnetic structure, and multi-card configuration" width="100%">
</p>

<p align="center"><sub>Physical card concept: the card triggers context; Host Service and Passport resolve it safely.</sub></p>

### V1 and V2 boundaries

| Version | Responsibility | Explicit non-goals |
| --- | --- | --- |
| V1 | Installed local Skills, manual writing, strict parsing, local testing, and IDE dispatch | No silent remote installation; no full prompt or command stored on the card |
| V2 direction | Immutable GitHub manifest revisions, trust-policy staging, validation, and registration | An untrusted card never silently downloads, executes, or receives credentials |

See the [writable NFC Skill card design](development/nfc-skill-card-design.md) for
the full schema and security boundary.

## What appears on the device

Passport is a prioritized physical state layer rather than a log viewer:

| Situation | Device response |
| --- | --- |
| Card tap | Scan line, single-card dwell, and offset card stack; invalid records show a protocol error |
| Task | Companion, task title, progress, and current session; a disconnect retains the last trusted state |
| Approval | Operation card with action, scope, source, and paginated details |
| Session | Long-press Up opens the picker; switching stays pending until the host confirms |
| Voice | The route is captured at start; releasing the confirmation key stops recording without retargeting |
| Companion | Host Service detects a PC-side change and syncs it in idle chunks; the device has no manual pet control |
| Battery | Four-cell pixel battery; unknown readings show a question mark rather than pretending to be empty |

<p align="center">
  <img src="../assets/images/passport-ui-v2/03-overview.zh.png" alt="AI Passport pixel UI overview showing session selection, session switching, and automatic PC companion synchronization" width="100%">
</p>

<p align="center"><sub>Multi-session and companion synchronization: task, voice, and approval remain tied to one IDE session.</sub></p>

<p align="center">
  <img src="../assets/images/passport-ui-v2/05-invalid-card.zh.png" alt="AI Passport pixel UI showing an invalid NFC card warning" width="320">
</p>

<p align="center"><sub>An invalid card does not start an IDE or change the current session.</sub></p>

## Current implementation

The current branch contains the main Passport Service v2 path:

- Exact Codex app-server thread/session routing and a bounded session catalog;
- Approval operation cards, details, per-request decisions, and receipts;
- 240 × 320 pixel UI, graphical battery, card stack, and offline retention;
- Voice route snapshots that prevent PCM delivery to the wrong conversation;
- PC companion discovery, 32 × 32 quantization, SHA-256 verification, and idle chunk transfer;
- A read-only Trae fallback and protocol 1 compatibility path;
- A design baseline for writable NFC Skill cards, currently bounded to V1 manual-write validation.

<p align="center">
  <img src="../assets/images/passport-ui-v2/pet-sync-flow.zh.png" alt="AI Passport flow showing PC companion detection, chunk transfer, and atomic replacement" width="100%">
</p>

Read the authoritative status, protocol, and UI documents:

- [Passport Service status](development/passport-service-status.md)
- [Passport v2 protocol](development/passport-v2-protocol.md)
- [Passport pixel companion UI design](development/passport-pixel-ui-design.md)
- [Writable NFC Skill card design](development/nfc-skill-card-design.md)

## Start developing

### Read the boundaries first

1. Read [`AGENTS.md`](../AGENTS.md) and the [AI development guide](development/ai-guide.md).
2. Check and install the five required skills: `passport-develop`, `passport-setup`,
   `passport-build`, `passport-device-test`, and `passport-debug`.
3. Use the [hardware guide](hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md) and
   [`components/bsp/include/bsp_pins.h`](../components/bsp/include/bsp_pins.h) as the
   sources of truth for board facts.
4. Keep application pages, state machines, and animation in `main`; keep reusable
   board logic in `components/bsp`.
5. Redesign application UI instead of reusing the hardware-test menu as a product.

### Start with one requirement

Use this prompt as a starting point and replace the product details:

```text
Build an offline habit-tracking application for AI Passport.
Use the 240 × 320 display and three physical buttons, and persist records across reboot.
Start from main on a feature/* branch after reading AGENTS.md and the hardware guide.
Keep hardware logic in components/bsp and application logic in main.
Redesign the application screens; do not reuse the hardware-test menu.
Run host tests and firmware validation, and report Build, Host tests, Device tests,
and remaining unverified hardware checks separately.
```

### Build and validate

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
./tools/validate.sh
```

A passing build is not hardware acceptance. Before flashing, run
`./tools/validate.sh --preflash`, confirm the target device and firmware, and
review the storage impact.

## Hardware baseline

**ESP32-C3 · 8 MB Flash · no PSRAM · 240 × 320 RGB565 · three physical buttons**

The default partition table contains NVS, PHY data, and one factory application
using the remaining space. Use [`bsp_pins.h`](../components/bsp/include/bsp_pins.h)
and the [hardware guide](hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md) for
display, buttons, audio, battery, shared I2C, Wi-Fi scan, and BLE boundaries.

## Documentation

| Topic | Documentation |
| --- | --- |
| Development rules and skills | [Development index](development/README.md) · [AI skills](../skills/README.md) |
| Host and protocols | [Passport v2 protocol](development/passport-v2-protocol.md) · [NFC Skill card](development/nfc-skill-card-design.md) |
| UI and assets | [Pixel UI design](development/passport-pixel-ui-design.md) · [Asset guide](../assets/README.md) |
| Hardware and build | [Hardware guide](hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md) · [Build and test](development/engineering/build-and-test.md) |
| Contribution and license | [Contributing](../.github/CONTRIBUTING.md) · [MIT License](../LICENSE) |

---

AI Passport is not a desktop window moved onto a device. It makes context,
boundaries, and decisions clear, physical, and verifiable.
