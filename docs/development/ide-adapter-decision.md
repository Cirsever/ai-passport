<p align="right">
  <a href="ide-adapter-decision.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# First IDE adapter decision — Codex (Trae fire-and-forget chat now shipping too)

Status: decided (Slice C · P0-3); Trae adapter landed 2026-09-15, scope
limited to fire-and-forget `trae-cn chat` — no bidirectional guarantees.
Owner: Passport Service.

## Decision

The first local IDE adapter is still **Codex**, because only Codex offers
the full stdio MCP round-trip, `elicitation/create` approval channel, and
`tools/call codex-reply` multi-turn continuation. **A Trae adapter is now
also optional**, but with limited semantics: it covers only the minimal
"operator taps card → Trae Chat window opens → utterance appended to the
same window" loop. task events, approval round-trip, and real session-id
feedback are synthesized by the adapter — Trae does not actually report
them.

This decision names the target IDE, its confirmed control surfaces, and the
acceptance surface for the adapter. It does not commit to a transport
strategy beyond what already ships; Slice C follow-ups continue to evolve
that.

## Why Codex, not Trae

- **Codex ships an installable CLI (`codex-cli` 0.139.0)** with documented
  non-interactive entry points, a stdio MCP server, and an experimental
  app-server daemon with remote control. All three can be exercised without
  a GUI, which is exactly what the host bridge needs to relay Passport
  gestures into the active IDE.
- **Trae only ships the desktop app** on this baseline (`/Applications/Trae.app`
  and `/Applications/Trae CN.app`). It exposes no documented local CLI, no
  stdio MCP server, and no scriptable daemon on macOS. Building an adapter
  today would mean guessing an unpublished control surface, which the
  architecture explicitly forbids.
- **Repository rule (`ai-guide.md`):** device firmware and the host bridge
  must not invent IDE control APIs. Codex meets the rule; Trae does not,
  yet.

Trae is not disqualified — the adapter contract below is IDE-agnostic. When
Trae publishes a scriptable surface (CLI or documented IPC), the same
contract slots in as a second adapter alongside Codex.

## Confirmed Codex control surfaces (macOS, this workstation)

Verified with `codex --version` and `codex <cmd> --help` on the developer
machine used for Slice F work. All three are documented public commands
shipped in the Codex CLI:

- **`codex exec [PROMPT]`** — non-interactive one-shot run. Accepts prompts
  from arg or stdin; each invocation spawns a fresh process and returns when
  the agent finishes. Suitable for stateless commands (`review`, single
  utterance) but not for long-lived Goal sessions.
- **`codex mcp-server`** — starts Codex as an MCP server over stdio. This is
  the primary target for the adapter because MCP framing is bidirectional,
  long-lived, and already understood by the host bridge Python.
- **`codex remote-control start|stop`** — starts the app-server daemon with
  remote control enabled. Marked experimental; treated as a fallback if
  `mcp-server` cannot cover a required capability (for example, multi-session
  handoff).

Additional Codex commands verified as available but out of scope for the
first adapter: `resume`, `apply`, `login`, `mcp`, `sandbox`. They may be
useful for later Passport features (session picker, apply-diff button, MCP
child management) but are not required for the goal.

## Adapter contract commitments

The first Codex adapter must, at minimum:

1. Speak `codex mcp-server` over stdio from a bridge-side Python subprocess.
2. Translate Passport Service protocol frames into Codex requests:
   - Passport `goal.mode.request` → open (or resume) a Codex session and
     reply with `goal.mode.state=enabled` including the Codex session id.
   - Passport `voice.capture.*` frames → deliver the utterance to the active
     Codex session as a user turn.
   - Codex progress events → Passport `task.state` + `task.event`.
   - Codex approval prompts → Passport `approval.request`; Passport
     `approval.decision` → Codex approval response.
3. Never persist Codex credentials on the device. All authentication happens
   through `codex login` on the host.
4. Log rejected messages via the Passport bridge's normal `> `/`< ` prefix
   so acceptance tests can diff them against the documented protocol.

## Deferred, out-of-scope for the decision

- Concrete Python module layout of the adapter (lives with the host bridge
  refactor, not the firmware).
- Codex's response streaming shape (chunked stdout vs MCP notifications) —
  will be measured against `mcp-server`, not guessed.
- Whether `codex remote-control` becomes primary. Only revisit if
  `mcp-server` proves insufficient for multi-session handoff or if Codex
  changes the daemon into the stable path.
- Trae adapter. Blocked until Trae ships a scriptable local surface.

## Acceptance for Slice C adapter (subsequent work)

Slice C will be considered complete only when all of these are true on the
physical device with the real Codex CLI:

- One NFC card event → Passport emits exactly one `goal.mode.request`.
- The adapter starts / resumes a Codex session and Passport receives
  `goal.mode.state=enabled` with a non-empty `session_id` within 30 s.
- Task progress, task events, and one approval round-trip render on the
  Wear pages without a mock CLI.
- `mock:` prefixes disappear from the acceptance script; the mock
  `!compose`/`!task` commands become optional developer utilities, not the
  primary end-to-end path.

Build, host tests, and device tests are reported separately, per the
existing validation gate.

## Follow-up trigger for Trae

Reopen the decision once **any one** of these becomes true:

- Trae ships an officially supported CLI or IPC on macOS with a documented
  session lifecycle.
- Trae exposes an MCP server compatible with the same envelope the Codex
  adapter uses.
- A user demand for Trae-first Passport support surfaces from real product
  telemetry, and the tradeoff cost of writing an unofficial adapter is
  explicitly accepted.

The first trigger is now **half-met**: Trae CN 3.3.98's
`/Applications/Trae CN.app/Contents/Resources/app/bin/trae-cn chat <prompt>`
is a documented official CLI reachable from the bridge, so a minimal
`tools/trae_adapter.py` shipped in this iteration (see "Trae adapter ·
fire-and-forget" below). It does not replace the Codex adapter — Trae still
lacks:

- machine-readable stdio replies (only window-rendering side effects);
- session-id feedback (`trae_adapter.py` synthesizes a `trae-<uuid8>`
  string so the Passport UI stays wire-compatible);
- an approval channel callback (`approval.decision` returns a
  `bridge.error`).

Until those three land, the Trae adapter is scoped to a "Passport card
opens a Trae Chat window" demo only; the production path stays on the
Codex adapter.

## Trae adapter · fire-and-forget (landed 2026-09-15)

**Scope** (only these):

- Passport `goal.mode.request` → one `trae-cn chat -m agent "..."` call;
  the adapter synthesizes `goal.mode.state=enabled` + `task.state` frames
  back to the device with a `trae-<uuid8>` session id.
- Passport `voice.capture.stop` carrying `text` → another `trae-cn chat`
  invocation that appends the utterance to the same Trae window, plus a
  synthesized `task.event` with a "delivered to Trae Chat" summary back to
  the device.
- Passport `approval.decision` → explicit `bridge.error`. Trae has no
  scriptable approval endpoint; operators must click inside the Trae
  window.

**Not in scope**: session persistence (each `trae-cn chat` is a fresh
process), real task-progress reporting, approval round-trip,
`voice.capture.audio` real audio path, multi-session switching.

**Wire command**:

```bash
python3 -u tools/passport_bridge.py --usb --serial /dev/cu.usbmodemXXX \
    --trae [--trae-cwd /path/to/project] [--trae-mode agent|ask|edit]
```

`--codex` and `--trae` are mutually exclusive. `--trae-binary` defaults to
auto-detection of `/Applications/Trae CN.app` and `/Applications/Trae.app`.

**Host tests**: `tests/test_trae_adapter.py` (11 cases including subprocess
stub, second-card rejection, UTF-8 CJK utterance, approval explicitly
unsupported, visibility hint) plus `tests/test_bridge_trae_glue.py` (5 cases
at the bridge glue layer, including NFC-relay pipeline routing).

**Field-demo lessons (2026-09-16)**:

- The first `--trae` field demo appeared to fail: the operator saw no new
  Trae Chat window. The window *was* there — `-r` reuse-window is the CLI
  default, so the prompt landed inside the currently-focused Trae window
  and merged with the ongoing conversation. Fix: adapter now spawns with
  `-n --maximize` so a distinct window opens every time. Workspace-storage
  side-channel confirmed each call adds a new `workspaceStorage/...` folder.
- Even with `-n`, the new window can stack behind the spawning window. The
  adapter now prints a stderr hint on each successful chat call telling the
  operator to reveal the new window with Mission Control (F3) or ⌘\`.
- The NFC-relay path also failed the same demo. The relay was queuing a
  `goal.mode.request` frame straight onto the wire, but that direction is
  device→host in the protocol — the device's line parser rejected it. Fix:
  `_drain_nfc_outbox` now dispatches the relay frame through the IDE
  pipeline (Codex or Trae) instead of forwarding it to the wire. Only the
  pipeline's replies (`goal.mode.state`, `task.state`) reach the device.
- Firmware side needed a one-card admission relaxation: `parse_goal_mode_state`
  used to require the card_id to already exist in `state->goal_card_id`
  (populated only by a real NFC reader hit). In the relay flow no reader
  ever fires. `state->goal_card_id` is now adopted from the first
  `goal.mode.state` on the wire; a second frame with a different card_id
  is still rejected — the "one card, no swap" rule is preserved.