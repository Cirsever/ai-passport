<p align="right">
  <a href="nfc-path-decision.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# NFC input path decision — phone-to-Bridge relay first

Status: decided (Slice D · P0-1).
Owner: Passport Service.

## Decision

The first Passport NFC input path is **phone-to-Bridge relay**. An external
PN532/MFRC522 reader wired to the ESP32-C3 is documented and stays available
as a follow-up, but does not gate the Slice C end-to-end MVP.

Both options terminate at the same in-firmware entry point:
`demo_passport_service_nfc_card(const char *card_id)`. Switching later is a
host-side change; no firmware refactor.

## Why the relay wins now

- **Zero hardware change.** The current baseline already ships an exposed
  passive NTAG213 that any modern phone can read. No BSP edits, no new
  bus, no power-budget conversation, no board re-spin.
- **Reuses the existing entry point.** The service core already exposes
  `demo_passport_service_nfc_card(card_id)` and rejects a second card once
  the first is admitted (`passport-service-architecture.md §5`). The relay
  is just a new host process that funnels the UID into that entry, which is
  exactly what `tools/passport_bridge.py`'s mock CLI already does.
- **Matches the MVP question we are answering.** MVP is proving the
  Passport → Bridge → Codex loop, not the industrial reliability of an
  RF reader. Postponing hardware work keeps Slice C unblocked.
- **Migration to an external reader is one file.** When the reader arrives,
  it becomes a BSP peripheral that emits the same normalised `card_id`
  string; both paths converge inside the service core.

## Why not the external reader as first path

- **New driver debt.** ESP-IDF 5.5.3 has no official PN532/MFRC522 driver;
  we would pull a third-party component, verify it against our own board,
  and own it. Not aligned with an MVP that is otherwise green.
- **Pin availability is tight.** `components/bsp/include/bsp_pins.h` already
  claims GPIO 0/1/2/3/4/5/6/7/8/9/10/20/21 for buttons/audio/I2C/LCD, and
  keeps 18/19 reserved for USB Serial/JTAG. That leaves 11/12/13 for the
  reader plus its IRQ line — usable, but every new pin is a fresh BSP
  audit for the whole team.
- **Power budget question.** PN532 draws ~100–150 mA at 3.3 V during a
  read burst. The CW2017 gauge and current LEDC settings can carry it,
  but the analysis is separate work that today buys nothing beyond user
  ergonomics.
- **Mechanical uncertainty.** The Passport enclosure does not have a
  documented NFC antenna window. Without measured RF characterisation we
  cannot promise a reliable read range. The [physical-skills MVP design
  doc](physical-skills-mvp-design.md#12-where-this-fits-in-the-slice-roadmap)
  already notes that Phase 0 RF characterisation must precede any
  in-Passport reader.

## Relay contract

Everything upstream of the firmware is a host concern. Constraints the
implementation must honour so the migration back to an external reader
stays cheap:

- **Same normalisation as the future reader.** The host publishes a
  `card_id` string that satisfies the existing normalisation rule in
  `demo_passport_service_nfc_card()` (alphanumerics plus `-_:.`,
  length < `PASSPORT_SERVICE_ID_MAX`). Reject anything else at the host
  before framing the `@passport goal.mode.request` line.
- **Single card per Passport session.** The service core rejects a
  second card. The relay must not paper over that with retries; if a
  second phone tap happens, surface it to the operator, do not resend.
- **No credentials on the device.** The phone-side app authenticates to
  the local Bridge over loopback or an operator-scoped token; the ESP32
  only ever sees the normalised `card_id`.
- **Debouncing lives on the host.** Multiple physical taps of the same
  phone within a short window must collapse into one Passport frame.
- **Failure is loud.** A relay drop is a `bridge.error` on the wire, not
  a fake success. Mirrors the Codex adapter's approval-stub convention.

## Concrete relay options (not part of the decision)

The decision picks the class, not the exact tooling. Any of these implement
the contract above; picking one is a follow-up task in Slice D:

- **iOS Shortcut → local HTTP.** Native NFC read, fires a shortcut that
  POSTs `card_id` to a Bridge endpoint on the developer's Mac. Zero user
  install, quickest to prototype.
- **Android Web NFC (Chrome).** Reads the NTAG213, uses a small
  self-hosted page that posts to the Bridge over `http://<mac>:PORT`.
  Also zero install, works on Android 12+ with Chrome.
- **Small native Android app.** Higher effort, but useful when the
  demo runs at a booth without a Wi-Fi lab network.

## Deferred, follow-up work

- Choose one relay option, wire the Bridge HTTP endpoint, hand off from
  `tools/passport_bridge.py` (a new `--nfc-relay-port` flag can share the
  same event loop as `--codex`).
- Log the relay handoff shape (framing, retry policy, TLS-or-loopback
  decision) in `docs/development/passport-service-status.md`.
- Reopen the reader path when any one of these becomes true:
  - Users complain the phone hop is too clunky for a wearable.
  - Passport hardware gets a documented NFC antenna window and we need
    the direct tap for demo credibility.
  - We build a fixed docking station where a wired reader is stationary
    and phone-independent.

Until then, the external reader is postponed by cost/benefit, not by
principle.

## Acceptance for the first relay implementation

- One real phone tap on the exposed NTAG213 causes exactly one
  `goal.mode.request` frame to reach the device.
- A second phone tap with a different UID is rejected on the wire (host
  emits `bridge.error`) — no second `goal.mode.request`.
- Bridge exit while a card is admitted does not silently drop the
  admission; the `link_idle_ms` stale banner still fires per the
  existing rules.
- Zero firmware code paths are added; changes are limited to `tools/`.