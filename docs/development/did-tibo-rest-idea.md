<p align="right">
  <a href="did-tibo-rest-idea.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# DidTiboRest on AI Passport

Status: exploratory idea. The current firmware prototype is not an approved
implementation baseline.

## Product idea

Use AI Passport as a small, physical companion for Tibo notifications. When a
Tibo push arrives, Passport gives an audible prompt so the user knows that an
action is waiting. If the user does not respond, Passport repeats the prompt
after a configurable interval. The user can mute the current prompt with a
button and switch the Codex level with a single button action.

## Intended interaction

1. Tibo sends a push event to the host-side bridge.
2. The bridge forwards the event to Passport.
3. Passport shows a compact title and message and plays a short sound.
4. If the event remains unhandled, Passport plays the reminder again after the
   reminder interval.
5. The user can mute the current event or change the Codex level.
6. A host-side response confirms the action and clears the pending reminder.

## Decisions still needed

- The real Tibo push channel and event schema.
- The meaning and number of Codex levels, and the host command that changes one.
- Whether mute applies to one event, one session, or a quiet period.
- Reminder interval, maximum retry count, escalation, and sound-volume rules.
- Button gestures that do not conflict with Passport's global navigation.
- Whether the first integration should use USB, TCP, BLE, or a replaceable
  transport interface.
- What must be persisted across reboot, if anything.

## Why the current prototype is not the baseline

The prototype uses a local JSON shape and a USB development frame. It does not
receive a verified Tibo event, issue a real Codex-level command, or prove the
full action acknowledgement loop. Its startup routing and diagnostic replies
were added only to make a hardware probe possible. The reminder policy and
button semantics also need product decisions before they should be treated as
the final experience.

## Restart point

The next implementation should begin with a host-side event contract and a
pure state-machine test matrix. Device transport, audio playback, display, and
button handling should be adapters around that contract. The first hardware
acceptance should cover one real push, one repeated reminder, mute, level
change, acknowledgement, and reconnect behavior.

## Integration with Passport Service

This idea is not a standalone firmware. It is Slice E of the Passport Service
architecture (see [`passport-service-architecture.md`](passport-service-architecture.md#slice-e-notification-companion-didtiborest))
and must reuse the existing envelope, page layout, and button gesture registry.

### Shared protocol envelope

All notification messages use the same `@passport ` newline-delimited JSON line
envelope as `task.state` / `goal.mode.*` / `approval.*`. Do not introduce a
second host↔device protocol; add `type` names into the Passport Service
protocol section first.

Host to Passport:

```json
{"type":"notify.push","event_id":"tibo-42","title":"Tibo","summary":"Action waiting","sound":"chime","reminder_ms":60000,"max_repeat":3}
{"type":"notify.level","level":"L2"}
{"type":"notify.state","event_id":"tibo-42","state":"cleared"}
```

Passport to host:

```json
{"type":"notify.mute","event_id":"tibo-42","scope":"event"}
{"type":"notify.level","level":"L2","source":"button"}
```

Rules:

- Unknown fields are ignored; unknown `type` names are rejected without
  mutating state, following the existing Passport Service rule.
- `notify.push` for an already-tracked `event_id` replaces the current record;
  it does not stack.
- `notify.state=cleared|resolved|failed` releases the reminder timer and hides
  the notification row.

### UI placement

Notifications occupy the collapsible event row in the Passport page layout
([section 7](passport-service-architecture.md#7-passport-page-layout)). The row
never covers the task summary or approval banner; while an approval is pending,
the notification row stays visible but reminders are suppressed until the
approval resolves.

### Button gestures

Notification and Codex-level actions are already registered in
[section 7.1 button gesture registry](passport-service-architecture.md#71-button-gesture-registry):

- `OK` short in notification-pending state mutes the current event.
- `OK` long (>= 1 s) in task view cycles Codex level and emits
  `notify.level` with `source=button`.
- `UP` and `DOWN` short retain their Passport Service meanings.

No new gesture is introduced without updating that table first.

### Audio and hardware dependencies

Sound playback reuses the ES8311 audio worker introduced in Slice D. This slice
does not add a new audio path, does not touch the NFC decision, and does not
change BSP wiring. Slice E is blocked on Slice D audio delivery.

### Persistence and reboot

At most the currently pending `event_id`, its remaining repeat count, and the
last Codex level survive reboot. They are stored in the shared NVS namespace so
the existing partition table and PC Wi-Fi state are not affected. If NVS is not
yet available for this slice, the device drops pending reminders on reboot and
waits for the bridge to replay them.

### Acceptance for Slice E

- One `notify.push` produces exactly one audible reminder and one visible row.
- A missed acknowledgement triggers `reminder_ms` repeats up to `max_repeat`.
- `OK` short during a notification emits one `notify.mute` and stops repeats.
- `OK` long emits one `notify.level` and the bridge acknowledges with
  `notify.level` from host.
- Approval pending suppresses the reminder tone without deleting the row.
- Reboot with the bridge offline: the device shows no ghost notification.
