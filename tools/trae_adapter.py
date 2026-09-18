#!/usr/bin/env python3
"""Trae CN / Trae MCP-less adapter for the Passport Service host bridge.

Trae exposes `trae-cn chat` (and `trae chat`) as a fire-and-forget CLI: it
posts a prompt to a currently-open Trae window and returns immediately. There
is no stdio MCP server, no approval callback, and no session-id round-trip.
This adapter takes exactly that surface and no more:

- `goal.mode.request` from Passport → spawn `trae-cn chat "<prompt>"` and
  echo a Passport `goal.mode.state=enabled` frame with a synthetic session id
  so the device confirms the operator's card and stops repainting the vertex.
- `voice.capture.stop` with a text payload → append another `trae-cn chat`
  invocation using the same session prompt shape, so an operator utterance
  lands in the same Trae window.
- Any other frame (approval, task events from Trae) → a bounded `bridge.error`
  so the wire contract stays honest. Approval round-trip in particular is
  documented as unsupported until Trae ships a scriptable approval endpoint.

Design constraints (identical to codex_adapter.py):
- No IDE credentials ever forwarded to the device.
- Anything Trae-specific stays inside this file; the device sees only the
  Passport envelope.
- Standard library only, so the host bridge keeps its "system Python
  everywhere" property.
"""

from __future__ import annotations

import argparse
import json
import shlex
import shutil
import subprocess
import sys
import time
import uuid
from typing import Any, Callable, Iterable


DEFAULT_MODE = "agent"
DEFAULT_BINARY = "trae-cn"
CANDIDATE_BINARIES = (
    DEFAULT_BINARY,
    "trae",
    "/Applications/Trae CN.app/Contents/Resources/app/bin/trae-cn",
    "/Applications/Trae.app/Contents/Resources/app/bin/trae",
)


def _resolve_binary(explicit: str | None) -> str:
    """Pick a working Trae CLI. Explicit path wins; otherwise probe the
    common macOS install locations plus PATH lookups. Raise a clear error
    when nothing is found so operators get actionable feedback."""
    if explicit:
        return explicit
    for candidate in CANDIDATE_BINARIES:
        # Absolute-path candidates: probe by existence.
        if "/" in candidate:
            try:
                # Guard against non-executable placeholders.
                import os
                if os.access(candidate, os.X_OK):
                    return candidate
            except OSError:
                continue
        else:
            found = shutil.which(candidate)
            if found:
                return found
    raise RuntimeError(
        "Trae CLI not found. Install Trae CN (or Trae) so one of "
        + ", ".join(CANDIDATE_BINARIES) + " is reachable, or pass --trae-binary.")


class TraeCliClient:
    """Very thin subprocess wrapper around `trae-cn chat`.

    Kept separate from the adapter so host tests can inject a fake client
    without spawning a real Trae window.
    """

    def __init__(self, binary: str, mode: str = DEFAULT_MODE,
                 reuse_window: bool = False, maximize: bool = True) -> None:
        # Default open behavior is `--new-window --maximize` so an operator
        # sees the Passport-driven Chat as a distinct window instead of it
        # merging into whatever Trae window is currently focused (which was
        # invisible to end users during the first --trae field demo).
        self._binary = binary
        self._mode = mode
        self._reuse_window = reuse_window
        self._maximize = maximize

    def send_chat(self, prompt: str, cwd: str | None = None,
                  extra_files: Iterable[str] | None = None,
                  timeout: float = 15.0) -> tuple[int, str, str]:
        """Fire one non-interactive chat prompt. Returns (returncode, stdout,
        stderr). trae-cn chat is fire-and-forget: the prompt lands in the
        current window and the CLI exits quickly. A non-zero return code
        or non-empty stderr is surfaced upstream as a `bridge.error` frame.
        """
        argv: list[str] = [self._binary, "chat", "-m", self._mode]
        if self._reuse_window:
            argv.append("-r")
        else:
            argv.append("-n")  # force a fresh, visible Trae window
        if self._maximize:
            argv.append("--maximize")
        for path in (extra_files or ()):
            argv.extend(["-a", path])
        argv.append(prompt)
        proc = subprocess.run(argv, cwd=cwd, capture_output=True, text=True,
                              timeout=timeout)
        return proc.returncode, proc.stdout, proc.stderr


class TraeAdapter:
    """Passport-frame façade around a `TraeCliClient`.

    Same handle() shape as CodexAdapter so the bridge glue can stay
    IDE-agnostic. The adapter never blocks; every device frame either fires
    one CLI call synchronously (Trae chat exits fast) or short-circuits with
    a bridge.error.
    """

    def __init__(self, client: TraeCliClient, *, cwd: str) -> None:
        self._client = client
        self._cwd = cwd
        self._active_card_id: str | None = None
        self._active_session_id: str | None = None
        self._on_emit: Callable[[dict[str, Any]], None] | None = None

    def set_emit_callback(self,
                          callback: Callable[[dict[str, Any]], None] | None) -> None:
        self._on_emit = callback

    def handle(self, frame: dict[str, Any]) -> list[dict[str, Any]]:
        emitted: list[dict[str, Any]] = []
        outgoing: Callable[[dict[str, Any]], None] = lambda x: emitted.append(x)
        try:
            frame_type = frame.get("type", "")
            if frame_type == "goal.mode.request":
                self._open_session(frame, outgoing)
            elif frame_type == "voice.capture.stop":
                self._forward_utterance(frame, outgoing)
            elif frame_type == "approval.decision":
                self._reject_approval(frame, outgoing)
            elif frame_type == "device.hello":
                pass
            else:
                # Unknown frames dropped silently to keep the bridge
                # pass-through friendly for future protocol additions.
                pass
        except Exception as exc:  # broad: never crash the bridge thread
            outgoing({"type": "bridge.error", "source": "trae_adapter",
                      "detail": str(exc)})
        if self._on_emit:
            for frame_out in emitted:
                self._on_emit(frame_out)
        return emitted

    def drain_pending_approvals(self,
                                out: Callable[[dict[str, Any]], None]) -> int:
        """Trae has no approval callback; nothing to drain. Returns 0 so the
        bridge main loop can call this uniformly whether the pipeline is
        Codex or Trae."""
        return 0

    # --- Trae actions --------------------------------------------------

    def _open_session(self, frame: dict[str, Any],
                      out: Callable[[dict[str, Any]], None]) -> None:
        card_id = frame.get("card_id", "")
        if not card_id:
            return
        if self._active_card_id and self._active_card_id != card_id:
            # Passport Service already rejects the second card; mirror the
            # rule on the adapter side so a stray host cannot double-open.
            return
        prompt = (
            f"You are Trae, running as the assistant for the Passport card "
            f"'{card_id}'. Wait for the operator's next utterance and reply "
            f"as normal.")
        code, stdout, stderr = self._client.send_chat(prompt, cwd=self._cwd)
        if code != 0:
            out({"type": "bridge.error", "source": "trae_adapter",
                 "detail": f"trae chat exited {code}: "
                           f"{(stderr or stdout).strip()[:180]}"})
            return
        # Trae CLI does not report a session identifier; synthesize one so
        # the wire contract stays symmetric with the Codex adapter. The
        # operator can correlate it against the Trae window that just
        # received the prompt.
        session_id = f"trae-{uuid.uuid4().hex[:8]}"
        self._active_card_id = card_id
        self._active_session_id = session_id
        # Visibility note printed to stderr so operators know where to look.
        # trae-cn with `-n --maximize` reliably spawns a fresh window, but
        # the newly-focused window can sit behind the Trae CN window that
        # spawned it, giving the impression that "nothing happened". The
        # first field demo hit exactly that snag.
        print(
            f"[trae-adapter] Trae Chat opened for card '{card_id}' "
            f"(session {session_id}). If you only see the current Trae CN "
            f"window, use Mission Control (F3) or ⌘` to reveal the new "
            f"one — it exists but is likely stacked behind the focused "
            f"window.",
            file=sys.stderr, flush=True)
        out({
            "type": "goal.mode.state",
            "mode": "goal",
            "state": "enabled",
            "card_id": card_id,
            "ide": "trae",
            "session_id": session_id,
        })
        out({
            "type": "task.state",
            "task_id": session_id,
            "state": "running",
            "progress": 0,
            "summary": "Trae Chat 已拉起，请在编辑器窗口继续对话",
        })

    def _forward_utterance(self, frame: dict[str, Any],
                           out: Callable[[dict[str, Any]], None]) -> None:
        if not self._active_session_id:
            return
        text = frame.get("text") or ""
        if not text:
            return
        code, stdout, stderr = self._client.send_chat(text, cwd=self._cwd)
        if code != 0:
            out({"type": "bridge.error", "source": "trae_adapter",
                 "detail": f"trae chat exited {code}: "
                           f"{(stderr or stdout).strip()[:180]}"})
            return
        # Trae Chat does not stream a machine-readable reply on stdout, so
        # we cannot echo Trae's answer onto the wire. Signal that the
        # utterance was delivered so the Passport UI moves on.
        out({
            "type": "task.event",
            "task_id": self._active_session_id,
            "event_id": f"trae-utter-{int(time.time())}",
            "ts": time.strftime("%H:%M"),
            "summary": "已投递到 Trae Chat",
        })

    def _reject_approval(self, frame: dict[str, Any],
                         out: Callable[[dict[str, Any]], None]) -> None:
        # Trae 3.3.98 has no scriptable approval callback. Emit an honest
        # bridge.error rather than pretending the decision landed inside
        # Trae. Callers who want approval round-trip should keep using the
        # Codex adapter until Trae exposes an MCP server or add-mcp callback.
        request_id = frame.get("request_id", "")
        decision = frame.get("decision", "")
        out({"type": "bridge.error", "source": "trae_adapter",
             "detail": ("trae adapter has no approval channel yet "
                        f"(request_id={request_id!r}, decision={decision!r}). "
                        "Approve inside the Trae window directly.")})


def _run_stdio_bridge(cwd: str, binary: str | None, mode: str) -> int:
    """Thin main entry mirroring codex_adapter._run_stdio_bridge.

    Reads one JSON frame per line on stdin, emits emitted frames on stdout.
    Real device wiring lives in passport_bridge.TraePipeline.
    """
    resolved = _resolve_binary(binary)
    client = TraeCliClient(resolved, mode=mode)
    adapter = TraeAdapter(client, cwd=cwd)
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            frame = json.loads(line)
        except json.JSONDecodeError as exc:
            print(json.dumps({"type": "bridge.error", "source": "trae_adapter",
                              "detail": f"invalid json: {exc}"},
                             ensure_ascii=False), flush=True)
            continue
        for reply in adapter.handle(frame):
            print(json.dumps(reply, ensure_ascii=False), flush=True)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cwd", default=".",
                        help="working directory forwarded to trae-cn chat")
    parser.add_argument("--trae-binary", default=None,
                        help="explicit path to trae-cn / trae; auto-detected "
                             "from /Applications/Trae*.app when omitted")
    parser.add_argument("--mode", default=DEFAULT_MODE,
                        help="trae chat mode: ask, edit, agent, or a custom "
                             "mode identifier (default: agent)")
    args = parser.parse_args()
    return _run_stdio_bridge(cwd=args.cwd, binary=args.trae_binary,
                             mode=args.mode)


if __name__ == "__main__":
    raise SystemExit(main())
