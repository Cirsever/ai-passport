#!/usr/bin/env python3
"""Codex MCP adapter for the Passport Service host bridge.

This module implements the Slice C contract locked in
docs/development/ide-adapter-decision.md. It runs on the developer's Mac /
PC alongside `tools/passport_bridge.py` and does exactly three things:

- Talks JSON-RPC to a `codex mcp-server` child process over stdio.
- Maps Passport Service protocol frames to Codex MCP calls
  (goal.mode.request / voice.capture.* / approval.decision).
- Translates Codex responses back into `@passport ` frames that
  `passport_bridge.py` can forward to the device.

Design constraints:
- No IDE credentials are ever forwarded to the device; the adapter relies on
  `codex login` state on the host.
- The Passport envelope is the only wire format the device sees. Any Codex
  concept that does not map cleanly stays inside this file.
- Nothing here imports anything outside the Python stdlib, so the host bridge
  keeps its "run everywhere with system Python" property.

The adapter is intentionally a library plus a `run()` entry point. The
device-facing side (framing, USB port handling) still lives in
`passport_bridge.py`; wiring the two together is deliberately left to a
separate follow-up (host `codex_bridge` binary) so this module can be tested
in isolation.
"""

from __future__ import annotations

import argparse
import json
import os
import queue
import subprocess
import sys
import threading
import time
import uuid
from typing import Any, Callable, Iterable

PROTOCOL_VERSION = "2025-06-18"
CLIENT_NAME = "passport-codex-adapter"
CLIENT_VERSION = "0.1.0"

# Default Codex knobs. These are conservative for MVP acceptance and can be
# overridden via CLI flags on the adapter binary.
DEFAULT_MODEL: str | None = None  # let Codex config.toml decide
DEFAULT_SANDBOX = "read-only"
DEFAULT_APPROVAL = "on-request"


class CodexMcpClient:
    """Minimal JSON-RPC client over stdio for `codex mcp-server`.

    Not a general-purpose MCP client — it only supports the initialize
    handshake, `tools/call`, and notification listening. Everything else is
    outside the P0-4 scope.
    """

    def __init__(self, argv: Iterable[str]) -> None:
        self._argv = list(argv)
        self._proc: subprocess.Popen[str] | None = None
        self._pending: dict[int, queue.Queue[dict[str, Any]]] = {}
        self._notifications: queue.Queue[dict[str, Any]] = queue.Queue()
        self._next_id = 1
        self._reader: threading.Thread | None = None
        self._lock = threading.Lock()

    def start(self) -> dict[str, Any]:
        self._proc = subprocess.Popen(
            self._argv,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()
        server_info = self._request("initialize", {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {},
            "clientInfo": {"name": CLIENT_NAME, "version": CLIENT_VERSION},
        }, timeout=10.0)
        self._notify("notifications/initialized")
        return server_info

    def stop(self) -> None:
        proc = self._proc
        if not proc:
            return
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        finally:
            self._proc = None

    def call_tool(self, name: str, arguments: dict[str, Any],
                  timeout: float = 60.0) -> dict[str, Any]:
        return self._request("tools/call", {"name": name, "arguments": arguments},
                             timeout=timeout)

    def pop_notification(self, timeout: float | None = 0.0) -> dict[str, Any] | None:
        try:
            return self._notifications.get(timeout=timeout) if timeout else \
                   self._notifications.get_nowait()
        except queue.Empty:
            return None

    # --- internals -----------------------------------------------------

    def _read_loop(self) -> None:
        assert self._proc is not None
        for raw in self._proc.stdout:
            line = raw.strip()
            if not line:
                continue
            try:
                message = json.loads(line)
            except json.JSONDecodeError:
                continue
            if "id" in message and ("result" in message or "error" in message):
                slot = self._pending.pop(message["id"], None)
                if slot is not None:
                    slot.put(message)
            elif "method" in message and "id" not in message:
                self._notifications.put(message)

    def _request(self, method: str, params: dict[str, Any] | None,
                 timeout: float) -> dict[str, Any]:
        with self._lock:
            request_id = self._next_id
            self._next_id += 1
            waiter: queue.Queue[dict[str, Any]] = queue.Queue(maxsize=1)
            self._pending[request_id] = waiter
        self._send({"jsonrpc": "2.0", "id": request_id, "method": method,
                    "params": params or {}})
        try:
            response = waiter.get(timeout=timeout)
        except queue.Empty as exc:
            self._pending.pop(request_id, None)
            raise TimeoutError(f"codex mcp-server did not answer {method} in "
                               f"{timeout:.1f}s") from exc
        if "error" in response:
            raise RuntimeError(f"codex mcp-server error on {method}: "
                               f"{response['error']}")
        return response.get("result", {})

    def _notify(self, method: str, params: dict[str, Any] | None = None) -> None:
        self._send({"jsonrpc": "2.0", "method": method, "params": params or {}})

    def _send(self, message: dict[str, Any]) -> None:
        assert self._proc is not None and self._proc.stdin is not None
        self._proc.stdin.write(json.dumps(message) + "\n")
        self._proc.stdin.flush()


class CodexAdapter:
    """Passport-frame façade around a `CodexMcpClient`.

    Callers hand this adapter a Passport-side frame (already JSON-decoded);
    the adapter emits zero or more Passport-side frames back as dicts. The
    outer bridge script decides how to serialise those (they will go through
    passport_bridge.encode_usb_line for USB delivery).
    """

    def __init__(self, client: CodexMcpClient, *, cwd: str,
                 model: str | None = DEFAULT_MODEL,
                 sandbox: str = DEFAULT_SANDBOX,
                 approval_policy: str = DEFAULT_APPROVAL) -> None:
        self._client = client
        self._cwd = cwd
        self._model = model
        self._sandbox = sandbox
        self._approval_policy = approval_policy
        self._active_card_id: str | None = None
        self._active_thread_id: str | None = None
        self._active_session_id: str | None = None
        self._on_emit: Callable[[dict[str, Any]], None] | None = None

    def set_emit_callback(self,
                          callback: Callable[[dict[str, Any]], None] | None) -> None:
        self._on_emit = callback

    # Public entry: consume one Passport-side frame.
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
                self._forward_approval_decision(frame, outgoing)
            elif frame_type == "device.hello":
                # No-op; the adapter has no state to sync on hello.
                pass
            else:
                # Unknown frames are dropped silently to keep the bridge
                # pass-through friendly for future protocol additions.
                pass
        except Exception as exc:  # broad: never crash the bridge thread
            outgoing({"type": "bridge.error", "source": "codex_adapter",
                      "detail": str(exc)})
        if self._on_emit:
            for frame_out in emitted:
                self._on_emit(frame_out)
        return emitted

    # --- Codex actions --------------------------------------------------

    def _open_session(self, frame: dict[str, Any],
                      out: Callable[[dict[str, Any]], None]) -> None:
        card_id = frame.get("card_id", "")
        if not card_id:
            return
        if self._active_card_id and self._active_card_id != card_id:
            # The Passport service already rejects the second card; the
            # adapter mirrors the rule so a rogue host cannot double-open.
            return
        prompt = ("You are Codex running as the assistant for the Passport "
                  f"card '{card_id}'. Wait for the operator's next utterance "
                  "and reply as normal.")
        arguments: dict[str, Any] = {
            "prompt": prompt,
            "cwd": self._cwd,
            "sandbox": self._sandbox,
            "approval-policy": self._approval_policy,
        }
        if self._model:
            arguments["model"] = self._model
        result = self._client.call_tool("codex", arguments, timeout=60.0)
        structured = _structured(result)
        thread_id = structured.get("threadId") if structured else None
        if not thread_id:
            out({"type": "bridge.error", "source": "codex_adapter",
                 "detail": "codex tool did not return threadId"})
            return
        self._active_card_id = card_id
        self._active_thread_id = thread_id
        self._active_session_id = f"codex-{thread_id[:8]}"
        out({
            "type": "goal.mode.state",
            "mode": "goal",
            "state": "enabled",
            "card_id": card_id,
            "ide": "codex",
            "session_id": self._active_session_id,
        })
        content = structured.get("content") if structured else ""
        if content:
            out({
                "type": "task.state",
                "task_id": self._active_session_id,
                "state": "running",
                "progress": 0,
                "summary": _summarise(content),
            })

    def _forward_utterance(self, frame: dict[str, Any],
                           out: Callable[[dict[str, Any]], None]) -> None:
        if not self._active_thread_id:
            return
        # For the P0-4 skeleton we assume the host has already transcribed the
        # audio into `text`. Real speech-to-text lives in Slice D + P0-5 and
        # will replace this branch with an audio pipeline.
        utterance = frame.get("text") or ""
        if not utterance:
            return
        result = self._client.call_tool("codex-reply", {
            "threadId": self._active_thread_id,
            "prompt": utterance,
        }, timeout=120.0)
        structured = _structured(result)
        content = structured.get("content") if structured else ""
        if not content:
            return
        out({
            "type": "task.event",
            "task_id": self._active_session_id or "codex",
            "event_id": f"codex-{uuid.uuid4().hex[:8]}",
            "ts": time.strftime("%H:%M"),
            "summary": _summarise(content),
        })
        out({
            "type": "task.state",
            "task_id": self._active_session_id or "codex",
            "state": "done",
            "progress": 100,
            "summary": _summarise(content),
        })

    def _forward_approval_decision(self, frame: dict[str, Any],
                                   out: Callable[[dict[str, Any]], None]) -> None:
        # Approval routing is a Codex-side MCP notification stream that we
        # have not yet inventoried on this workstation. This branch keeps
        # the wire contract honest by acknowledging the Passport decision
        # without pretending it landed inside Codex. Slice C follow-up work
        # replaces this with a real correlation once the approval channel
        # is confirmed.
        request_id = frame.get("request_id", "")
        decision = frame.get("decision", "")
        out({"type": "bridge.error", "source": "codex_adapter",
             "detail": "approval.decision received but Codex approval bridge "
                       f"is stubbed (request_id={request_id!r}, "
                       f"decision={decision!r})"})


def _structured(result: dict[str, Any]) -> dict[str, Any]:
    """Codex responses land under result.structuredContent per the MCP spec.

    Some Codex CLI versions inline the same fields at the top level; the
    helper flattens both shapes.
    """
    structured = result.get("structuredContent")
    if isinstance(structured, dict):
        return structured
    return {k: result[k] for k in ("threadId", "content") if k in result}


def _summarise(content: str, limit: int = 90) -> str:
    text = " ".join(content.split())
    if len(text) <= limit:
        return text
    return text[: limit - 1] + "…"


def _run_stdio_bridge(cwd: str, model: str | None, sandbox: str,
                      approval_policy: str) -> int:
    """Very thin main entry: read Passport frames on stdin, emit frames on stdout.

    Used by manual testing and by the acceptance script; the production wiring
    into passport_bridge.py is out of scope for this file.
    """
    client = CodexMcpClient(["codex", "mcp-server"])
    server_info = client.start()
    print(json.dumps({"type": "bridge.hello", "codex": server_info}),
          flush=True)
    adapter = CodexAdapter(client, cwd=cwd, model=model, sandbox=sandbox,
                           approval_policy=approval_policy)
    adapter.set_emit_callback(lambda f: print(json.dumps(f, ensure_ascii=False),
                                              flush=True))
    try:
        for raw_line in sys.stdin:
            line = raw_line.strip()
            if not line:
                continue
            try:
                frame = json.loads(line)
            except json.JSONDecodeError as exc:
                print(json.dumps({"type": "bridge.error",
                                  "source": "codex_adapter",
                                  "detail": f"bad json: {exc}"}), flush=True)
                continue
            adapter.handle(frame)
    finally:
        client.stop()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cwd", default=os.getcwd(),
                        help="Working directory Codex sees for the session.")
    parser.add_argument("--model", default=None,
                        help="Optional Codex model override.")
    parser.add_argument("--sandbox", default=DEFAULT_SANDBOX,
                        choices=("read-only", "workspace-write",
                                 "danger-full-access"))
    parser.add_argument("--approval-policy", default=DEFAULT_APPROVAL,
                        choices=("untrusted", "on-failure", "on-request",
                                 "never"))
    args = parser.parse_args()
    return _run_stdio_bridge(args.cwd, args.model, args.sandbox,
                             args.approval_policy)


if __name__ == "__main__":
    raise SystemExit(main())
