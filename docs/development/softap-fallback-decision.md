<p align="right">
  <a href="softap-fallback-decision.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# SoftAP / TCP fallback decision — keep, do not enable by default

Status: decided.
Owner: Passport Service.

## Decision

**Keep** the SoftAP + TCP transport code path in-tree, **do not enable it by
default**, and **do not require the developer to configure host Wi-Fi** for
Passport Service usage. USB Serial/JTAG is the primary and only default
transport for MVP delivery.

Nothing about this decision touches PC Wi-Fi state, per the recurring rule in
`docs/development/passport-service-status.md`.

## Why keep it

- **Zero regression cost.** `main/passport_transport_tcp.c` already exists,
  compiles, and does not consume any pin/GPIO that the USB path needs. It
  is enabled by an optional demo entry rather than by the Passport Service
  page, so keeping it in-tree does not slow the default boot path.
- **Airgapped-demo insurance.** There is one plausible scenario where USB
  Serial/JTAG is unusable: an on-stage demo where the operator only has a
  battery + wireless Passport and a phone running the Bridge. TCP + a
  temporary SoftAP handles that case without any new module.
- **Symmetric with the DidTiboRest exploration.** The Tibo push idea has
  always considered BLE/Wi-Fi as candidate transports. Deleting the TCP
  scaffolding today would immediately re-open that discussion the moment a
  wireless demo is asked for.

## Why not the default

- **DTR unreliability is a USB problem, not a TCP problem** — but adding a
  TCP fallback next to USB doubles the surface for the same "link
  liveness" concept and the acceptance rule (`link_idle_ms > 30 s`) still
  applies. Two ways to run the bridge means two ways for the operator to
  land in the "unknown state" bucket.
- **SoftAP would touch the very NVS partition we explicitly refuse to
  churn.** Once the Passport starts advertising, Wi-Fi credentials that
  the device or host cached become part of the acceptance surface. USB
  keeps NVS quiet.
- **Passport-first UX** implies the phone is the operator device (see the
  NFC relay decision). If the phone speaks HTTP to the Mac, adding a
  SoftAP path where the phone speaks Wi-Fi to the Passport is a
  duplicate transport, not an upgrade.

## Follow-up (only if the triggers below fire)

- Operator explicitly requests a wireless-only demo, or on-stage
  connectivity forces it.
- USB Serial/JTAG proves unreliable on a broader hardware population than
  the current dev board.
- A future slice needs a persistent Passport ↔ phone data channel that
  USB cannot carry.

Any of the above reopens the decision. Until then, the SoftAP demo stays
present but silent, and the Passport Service transport gate remains USB
Serial/JTAG only.

## What this means for `--codex` and `--nfc-relay`

Both host-side flags in `tools/passport_bridge.py` already read from the
Passport wire, not from the transport layer. Adding SoftAP later requires
zero changes to either path — they attach to the same in-process
`send_json` sink regardless of whether the underlying connection is a
TCP socket or a USB serial fd.

## Acceptance

- No firmware code paths change as a result of this decision.
- `tools/passport_bridge.py` continues to expose `--usb` as the default
  and TCP as opt-in via `--host/--port`.
- `docs/development/passport-service-todo.md` marks the TCP/SoftAP
  question resolved; a future re-open is a new task, not a re-litigation.