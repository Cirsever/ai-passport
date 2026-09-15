<p align="right">
  <a href="ide-adapter-decision.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# First IDE adapter decision — Codex

Status: decided (Slice C · P0-3).
Owner: Passport Service.

## Decision

The first local IDE adapter is **Codex**. Trae is deferred until it exposes a
non-GUI local control surface that can be scripted from the host bridge.

This decision only names the target IDE, its confirmed control surfaces, and
the acceptance surface for the adapter. It intentionally does not commit to
implementation code, transport strategy, or session model. Those land in
follow-up work under Slice C.

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

Until then, the Trae adapter is postponed by rule, not by preference.