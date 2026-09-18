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
import hashlib
import json
import os
import queue
import secrets
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
        self._server_requests: queue.Queue[dict[str, Any]] = queue.Queue()
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

    def pop_server_request(self, timeout: float | None = 0.0) -> dict[str, Any] | None:
        """Return the next JSON-RPC request the server issued to us (e.g.
        `elicitation/create` for exec / apply_patch approvals), or None if
        nothing is pending. The caller MUST later respond via
        respond_to_server_request(request_id, result_or_error); leaving a
        server request unanswered stalls Codex on the approval turn."""
        try:
            return self._server_requests.get(timeout=timeout) if timeout else \
                   self._server_requests.get_nowait()
        except queue.Empty:
            return None

    def respond_to_server_request(self, request_id: int | str,
                                  result: dict[str, Any] | None = None,
                                  error: dict[str, Any] | None = None) -> None:
        """Send the JSON-RPC response for a request the server sent us."""
        message: dict[str, Any] = {"jsonrpc": "2.0", "id": request_id}
        if error is not None:
            message["error"] = error
        else:
            message["result"] = result if result is not None else {}
        self._send(message)

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
            elif "method" in message and "id" in message:
                # Server-initiated JSON-RPC request (e.g. elicitation/create
                # for exec_approval_request / apply_patch_approval_request).
                # The caller drains these via pop_server_request() and MUST
                # respond via respond_to_server_request(), otherwise Codex
                # will block waiting for the approval turn to complete.
                self._server_requests.put(message)
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


class CodexAppServerClient(CodexMcpClient):
    """JSON-RPC client for the exact-thread Codex app-server API."""

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
        result = self._request("initialize", {
            "clientInfo": {
                "name": CLIENT_NAME,
                "title": "Passport Codex adapter",
                "version": CLIENT_VERSION,
            },
            "capabilities": {},
        }, timeout=10.0)
        self._notify("initialized")
        return result

    def request(self, method: str, params: dict[str, Any],
                timeout: float = 30.0) -> dict[str, Any]:
        return self._request(method, params, timeout=timeout)


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
                 approval_policy: str = DEFAULT_APPROVAL,
                 session_client: CodexAppServerClient | None = None,
                 bridge_id: str | None = None) -> None:
        self._client = client
        self._session_client = session_client
        self._cwd = cwd
        self._model = model
        self._sandbox = sandbox
        self._approval_policy = approval_policy
        self._bridge_id = bridge_id or secrets.token_hex(8)
        self._route_epoch = 0
        self._route_sid: str | None = None
        self._route_title = ""
        self._route_threads: dict[str, str] = {}
        self._last_session_tx = 0
        self._protocol = 1
        self._active_card_id: str | None = None
        self._active_thread_id: str | None = None
        self._active_session_id: str | None = None
        self._on_emit: Callable[[dict[str, Any]], None] | None = None
        # Approval bridge: when Codex issues an elicitation/create request we
        # remember request_id -> rpc_id so the Passport approval.decision frame
        # can route the operator's answer back to the right pending RPC.
        # request_id here is the Passport-side identifier we emit on
        # approval.request; the value is the MCP request id we must respond to.
        self._pending_approvals: dict[str, int | str] = {}
        self._pending_approval_clients: dict[str, CodexMcpClient] = {}
        self._pending_approval_styles: dict[str, str] = {}
        self._approval_details: dict[str, str] = {}
        self._queued_route_approvals: list[dict[str, Any]] = []
        self._next_approval_seq = 1

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
            elif frame_type == "approval.detail":
                self._approval_detail(frame, outgoing)
            elif frame_type == "device.hello":
                self._hello(frame, outgoing)
            elif frame_type == "session.list":
                self._list_sessions(frame, outgoing)
            elif frame_type in ("session.query", "session.cancel"):
                self._emit_selected(int(frame.get("tx", 0)), outgoing)
            elif frame_type == "session.select":
                self._select_session(frame, outgoing)
            elif frame_type == "companion.ack":
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

    def _hello(self, frame: dict[str, Any],
               out: Callable[[dict[str, Any]], None]) -> None:
        protocol = frame.get("protocol")
        self._protocol = 2 if protocol == 2 and self._session_client else 1
        out({
            "type": "host.hello",
            "protocol": self._protocol,
            "bridge": self._bridge_id,
            "sessions": self._protocol == 2,
            "companion": self._protocol == 2,
        })
        if self._protocol == 2:
            self._emit_selected(int(frame.get("tx", 0)), out)

    def _sid_for_thread(self, thread_id: str) -> str:
        digest = hashlib.sha256(
            f"{self._bridge_id}\0{thread_id}".encode()).hexdigest()[:16]
        sid = f"s-{digest}"
        self._route_threads[sid] = thread_id
        return sid

    @staticmethod
    def _thread_title(thread: dict[str, Any]) -> str:
        title = thread.get("name") or thread.get("preview") or "Untitled"
        return _summarise(str(title), limit=56)

    @staticmethod
    def _thread_state(thread: dict[str, Any]) -> str:
        status = thread.get("status")
        if isinstance(status, dict):
            value = status.get("type", "idle")
        else:
            value = status or "idle"
        return {
            "active": "running",
            "idle": "idle",
            "notLoaded": "idle",
            "systemError": "ended",
        }.get(str(value), "idle")

    def _list_sessions(self, frame: dict[str, Any],
                       out: Callable[[dict[str, Any]], None]) -> None:
        if self._protocol != 2 or not self._session_client:
            return
        tx = frame.get("tx")
        page = frame.get("page")
        if not isinstance(tx, int) or not isinstance(page, int) or page < 0:
            return
        result = self._session_client.request("thread/list", {
            "cwd": [self._cwd],
            "limit": min((page + 1) * 3, 99),
            "sortKey": "updated_at",
            "sortDirection": "desc",
        })
        threads = result.get("data")
        if not isinstance(threads, list):
            threads = []
        selected = threads[page * 3:page * 3 + 3]
        out({
            "type": "session.catalog",
            "tx": tx,
            "page": page,
            "count": len(selected),
            "total": len(threads) + (1 if result.get("nextCursor") else 0),
        })
        for index, thread in enumerate(selected):
            thread_id = str(thread.get("id") or "")
            if not thread_id:
                continue
            out({
                "type": "session.entry",
                "tx": tx,
                "index": index,
                "sid": self._sid_for_thread(thread_id),
                "title": self._thread_title(thread),
                "ide": "codex",
                "state": self._thread_state(thread),
                "writable": True,
            })

    def _select_session(self, frame: dict[str, Any],
                        out: Callable[[dict[str, Any]], None]) -> None:
        if self._protocol != 2 or not self._session_client:
            return
        tx = frame.get("tx")
        sid = frame.get("sid")
        if not isinstance(tx, int) or tx < self._last_session_tx:
            return
        if not isinstance(sid, str) or sid not in self._route_threads:
            out({"type": "session.error", "tx": tx, "reason": "unknown"})
            return
        thread_id = self._route_threads[sid]
        try:
            result = self._session_client.request(
                "thread/read", {"threadId": thread_id, "includeTurns": False})
            thread = result.get("thread") or {}
            self._session_client.request(
                "thread/resume", {"threadId": thread_id, "excludeTurns": True})
        except Exception as exc:
            out({"type": "session.error", "tx": tx,
                 "reason": _summarise(str(exc), 56)})
            return
        self._last_session_tx = tx
        self._route_epoch += 1
        self._route_sid = sid
        self._route_title = self._thread_title(thread)
        self._active_thread_id = thread_id
        self._emit_selected(tx, out)
        queued = self._queued_route_approvals
        self._queued_route_approvals = []
        for approval in queued:
            if approval.pop("_thread_id", None) == thread_id:
                out(approval)
            else:
                self._queued_route_approvals.append(approval)

    def _emit_selected(self, tx: int,
                       out: Callable[[dict[str, Any]], None]) -> None:
        out({
            "type": "session.selected",
            "tx": tx,
            "bridge": self._bridge_id,
            "epoch": self._route_epoch,
            "sid": self._route_sid or "",
            "ide": "codex",
            "title": self._route_title,
            "state": "idle",
            "summary": "",
            "progress": 0,
            "writable": bool(self._route_sid),
        })

    def active_route(self) -> dict[str, Any] | None:
        if self._protocol != 2 or not self._route_sid:
            return None
        return {
            "bridge": self._bridge_id,
            "sid": self._route_sid,
            "epoch": self._route_epoch,
        }

    def _route_matches(self, frame: dict[str, Any]) -> bool:
        route = self.active_route()
        return route is not None and all(frame.get(key) == value
                                         for key, value in route.items())

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
        # passport_bridge optionally replaces the device diagnostic with
        # host-side STT output before this frame reaches the adapter.
        utterance = frame.get("text") or ""
        if not utterance:
            return
        if self._protocol == 2:
            if not self._route_matches(frame) or not self._session_client:
                out({"type": "bridge.error", "source": "codex_adapter",
                     "detail": "voice route is stale"})
                return
            self._session_client.request("turn/start", {
                "threadId": self._active_thread_id,
                "input": [{"type": "text", "text": utterance}],
            }, timeout=30.0)
            route = self.active_route() or {}
            out({
                "type": "task.state",
                **route,
                "task_id": self._route_sid or "codex",
                "state": "running",
                "progress": 0,
                "summary": _summarise(utterance),
            })
            return
        result = self._client.call_tool("codex-reply", {
            "threadId": self._active_thread_id, "prompt": utterance,
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
        # Codex routes exec_approval_request / apply_patch_approval_request
        # through the MCP `elicitation/create` server-initiated request. The
        # Passport-side approval.request frame carries the request_id we
        # generated in drain_pending_approvals(); look up the RPC id and reply
        # with the ReviewDecision variant matching the operator's choice.
        request_id = frame.get("request_id", "")
        decision = frame.get("decision", "")
        if not request_id or not decision:
            out({"type": "bridge.error", "source": "codex_adapter",
                 "detail": "approval.decision missing request_id or decision"})
            return
        if decision not in ("approve", "reject"):
            out({"type": "bridge.error", "source": "codex_adapter",
                 "detail": f"invalid approval decision={decision!r}"})
            return
        if self._protocol == 2 and not self._route_matches(frame):
            out({"type": "approval.receipt", "request_id": request_id,
                 "status": "unknown", **(self.active_route() or {})})
            return
        rpc_id = self._pending_approvals.get(request_id)
        if rpc_id is None:
            out({"type": "bridge.error", "source": "codex_adapter",
                 "detail": f"unknown approval request_id={request_id!r}"})
            return
        self._pending_approvals.pop(request_id, None)
        client = self._pending_approval_clients.pop(request_id, self._client)
        style = self._pending_approval_styles.pop(request_id, "mcp")
        self._approval_details.pop(request_id, None)
        # Passport wire uses "approve" / "reject"; Codex's ReviewDecision enum
        # uses "approved" / "denied" / "approved_for_session" / "abort". Map
        # conservatively: only "approve" translates to a positive answer.
        if style == "app":
            review = "accept" if decision == "approve" else "decline"
        else:
            review = "approved" if decision == "approve" else "denied"
        client.respond_to_server_request(
            rpc_id, result={"decision": review})
        if self._protocol == 2:
            out({
                "type": "approval.receipt",
                **(self.active_route() or {}),
                "request_id": request_id,
                "status": "allowed" if decision == "approve" else "denied",
            })

    def _approval_detail(self, frame: dict[str, Any],
                         out: Callable[[dict[str, Any]], None]) -> None:
        request_id = frame.get("request_id")
        page = frame.get("page")
        if (self._protocol != 2 or not self._route_matches(frame) or
                not isinstance(request_id, str) or
                not isinstance(page, int) or page != 0 or
                request_id not in self._approval_details):
            return
        out({
            "type": "approval.page",
            **(self.active_route() or {}),
            "request_id": request_id,
            "page": 0,
            "pages": 1,
            "text": self._approval_details[request_id],
        })

    def _queue_approval(self, req: dict[str, Any],
                        client: CodexMcpClient, style: str,
                        operation: str, summary: str, detail: str,
                        out: Callable[[dict[str, Any]], None]) -> None:
        rpc_id = req.get("id")
        params = req.get("params") or {}
        request_id = f"codex-appr-{self._next_approval_seq}"
        self._next_approval_seq += 1
        self._pending_approvals[request_id] = rpc_id
        self._pending_approval_clients[request_id] = client
        self._pending_approval_styles[request_id] = style
        self._approval_details[request_id] = _summarise(detail, 90)
        approval = {
            "type": "approval.request",
            "request_id": request_id,
            "summary": _summarise(summary),
        }
        thread_id = params.get("threadId")
        if self._protocol == 2:
            sid = self._sid_for_thread(str(thread_id)) if thread_id else None
            route = self.active_route()
            if not route or sid != route["sid"]:
                approval["_thread_id"] = thread_id
                self._queued_route_approvals.append(approval)
                return
            approval.update(route)
            approval.update({
                "operation": operation,
                "pages": 1,
                "allow": True,
                "remaining_ms": 60000,
            })
        out(approval)

    def drain_pending_approvals(self,
                                out: Callable[[dict[str, Any]], None]) -> int:
        """Drain server-initiated approval requests from the MCP client and
        translate each into a Passport `approval.request` frame. Returns the
        number of approvals surfaced. Callers should poll this on the same
        cadence they drain notifications."""
        n = 0
        while True:
            req = self._client.pop_server_request(timeout=0)
            if not req:
                return n
            method = req.get("method", "")
            rpc_id = req.get("id")
            params = req.get("params") or {}
            if method != "elicitation/create" or rpc_id is None:
                # Unknown server request: refuse politely so Codex doesn't
                # stall. -32601 = method not found in JSON-RPC.
                self._client.respond_to_server_request(
                    rpc_id or 0,
                    error={"code": -32601, "message": f"unsupported {method}"})
                continue
            # elicitation/create carries a schema + message; surface the
            # message text as the approval summary. The Passport UI only
            # needs a short line so operators can decide.
            summary = self._summarise_elicitation(params)
            self._queue_approval(
                req, self._client, "mcp", "command", summary, summary, out)
            n += 1

        # Unreachable because the loop returns when the MCP queue is empty.

    def drain_app_server_approvals(
            self, out: Callable[[dict[str, Any]], None]) -> int:
        if not self._session_client:
            return 0
        count = 0
        while True:
            req = self._session_client.pop_server_request(timeout=0)
            if not req:
                return count
            method = req.get("method", "")
            params = req.get("params") or {}
            if method == "item/commandExecution/requestApproval":
                command = str(params.get("command") or params.get("reason") or
                              "Run command")
                cwd = str(params.get("cwd") or self._cwd)
                self._queue_approval(
                    req, self._session_client, "app", "command",
                    command, f"{command}\n{cwd}", out)
                count += 1
            elif method == "item/fileChange/requestApproval":
                reason = str(params.get("reason") or "Apply file changes")
                self._queue_approval(
                    req, self._session_client, "app", "edit",
                    reason, reason, out)
                count += 1
            else:
                self._session_client.respond_to_server_request(
                    req.get("id", 0),
                    error={"code": -32601, "message": f"unsupported {method}"})

    @staticmethod
    def _summarise_elicitation(params: dict[str, Any]) -> str:
        # MCP elicitation/create schema: {"message": str, "requestedSchema": ...}.
        # Fall back to any text-like field if the message key is absent.
        for key in ("message", "prompt", "title", "summary"):
            value = params.get(key)
            if isinstance(value, str) and value.strip():
                return _summarise(value.strip())
        return _summarise(json.dumps(params, ensure_ascii=False))


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
    encoded = text.encode("utf-8")
    if len(encoded) <= limit:
        return text
    prefix = encoded[:max(0, limit - 3)]
    while prefix:
        try:
            return prefix.decode("utf-8") + "..."
        except UnicodeDecodeError:
            prefix = prefix[:-1]
    return "..."


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
