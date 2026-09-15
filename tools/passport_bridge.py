#!/usr/bin/env python3
"""Small standard-library bridge for the Passport development protocol.

The device creates a temporary Wi-Fi access point and accepts one newline-
delimited JSON session. This bridge is intentionally a development tool, not a
production Agent transport: it has no authentication beyond the temporary AP
password and does not execute IDE actions.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import pathlib
import queue
import re
import select
import socket
import sys
import termios
import threading
import tty
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

USB_FRAME_PREFIX = "@passport "

# demo_passport_service.c enforces the same rule; we reject early on the host
# so a malformed phone tap never puts pressure on the device parser.
_CARD_ID_RE = re.compile(r"^[A-Za-z0-9\-_:.]{1,47}$")


HELP_TEXT = (
    "Commands (prefix with '!'):\n"
    "  !compose <tile> [tile ...]        emit tile.stack.state + context.composed\n"
    "  !compose clear                    emit an empty stack\n"
    "  !event <summary>                  emit a task.event line\n"
    "  !skill <skill_id> <revision>      emit a skill.updated line\n"
    "  !task <state> <progress> <summary> emit a task.state line\n"
    "  !approval <summary>               emit an approval.request line\n"
    "  !help                             show this help\n"
)


def _load_codex_adapter() -> Any:
    """Import tools/codex_adapter.py without polluting sys.path.

    The adapter lives next to this bridge on disk but ``passport_bridge`` is
    already imported as a top-level module by validate.sh, so a plain
    ``import codex_adapter`` might miss it depending on how the caller runs
    the bridge. This helper loads it by path when --codex is requested.
    """
    here = pathlib.Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location(
        "codex_adapter", here / "codex_adapter.py")
    if not spec or not spec.loader:
        raise RuntimeError("tools/codex_adapter.py not found next to the bridge")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CodexPipeline:
    """Optional device → Codex → device tap for --codex mode.

    The bridge feeds every decoded device-side frame to ``dispatch`` and gets
    zero or more Passport frames back that need to be forwarded to the
    device. The connection object is not held inside the pipeline; the
    caller re-sends via its usual ``send_json`` path so all outbound traffic
    keeps flowing through the single existing serialisation point.
    """

    def __init__(self, cwd: str, model: str | None, sandbox: str,
                 approval_policy: str) -> None:
        module = _load_codex_adapter()
        self._client = module.CodexMcpClient(["codex", "mcp-server"])
        info = self._client.start()
        version = info.get("serverInfo", {}).get("version", "unknown")
        print(f"codex mcp-server ready (version={version})", flush=True)
        self._adapter = module.CodexAdapter(
            self._client, cwd=cwd, model=model, sandbox=sandbox,
            approval_policy=approval_policy,
        )

    def dispatch(self, device_frame: dict[str, Any]) -> list[dict[str, Any]]:
        return self._adapter.handle(device_frame)

    def close(self) -> None:
        try:
            self._client.stop()
        except Exception:
            pass


class NfcRelayServer:
    """HTTP endpoint that phones POST NFC UIDs to.

    Layout is intentionally minimal:
      POST /nfc {"card_id": "<uid>"} → 202 {"ok": true} on success
                                       → 400 with an explanatory body on rejection
    Anything else, including GET, returns 405.

    The server appends validated `goal.mode.request` frames onto ``queue``.
    The bridge main loop pops them each tick and pushes them to the device
    via the regular send_json path. The queue is passed in from the caller
    so we do not need to know whether the transport is USB or TCP.
    """

    def __init__(self, host: str, port: int,
                 outbox: "queue.Queue[dict[str, Any]]",
                 debounce_window_ms: int = 1500) -> None:
        self._outbox = outbox
        self._debounce_window_ms = debounce_window_ms
        self._last_card_id: str | None = None
        self._last_at_ms: float = 0.0
        self._lock = threading.Lock()

        outbox_ref = self
        # Nested class captures outbox_ref so we can subclass without pushing
        # global state around.
        class _Handler(BaseHTTPRequestHandler):
            def log_message(self, format: str, *args: Any) -> None:  # noqa: A003
                print(f"[nfc-relay] {format % args}", flush=True)

            def _reject(self, status: int, message: str) -> None:
                body = json.dumps({"ok": False, "error": message}).encode()
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_POST(self) -> None:  # noqa: N802
                if self.path != "/nfc":
                    self._reject(404, "unknown path")
                    return
                length = int(self.headers.get("Content-Length", "0") or 0)
                raw = self.rfile.read(length) if length > 0 else b""
                try:
                    payload = json.loads(raw.decode("utf-8") or "{}")
                except json.JSONDecodeError as exc:
                    self._reject(400, f"bad json: {exc}")
                    return
                if not isinstance(payload, dict):
                    self._reject(400, "payload must be an object")
                    return
                card_id = payload.get("card_id")
                if not isinstance(card_id, str) or not _CARD_ID_RE.match(card_id):
                    self._reject(400, "card_id must match ^[A-Za-z0-9\\-_:.]{1,47}$")
                    return
                accepted = outbox_ref._accept(card_id)
                if not accepted:
                    self._reject(429, "debounced same UID")
                    return
                body = json.dumps({"ok": True, "card_id": card_id}).encode()
                self.send_response(202)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self) -> None:  # noqa: N802
                self._reject(405, "POST /nfc only")

        self._server = ThreadingHTTPServer((host, port), _Handler)
        self._thread: threading.Thread | None = None
        self._bound_host = host
        self._bound_port = self._server.server_address[1]

    def start(self) -> None:
        self._thread = threading.Thread(
            target=self._server.serve_forever, name="nfc-relay", daemon=True)
        self._thread.start()
        print(f"NFC relay listening on http://{self._bound_host}:"
              f"{self._bound_port}/nfc", flush=True)

    def close(self) -> None:
        try:
            self._server.shutdown()
        except Exception:
            pass

    # --- accept + debounce -------------------------------------------------

    def _accept(self, card_id: str) -> bool:
        import time
        now_ms = time.monotonic() * 1000.0
        with self._lock:
            if (self._last_card_id == card_id and
                    now_ms - self._last_at_ms < self._debounce_window_ms):
                return False
            self._last_card_id = card_id
            self._last_at_ms = now_ms
        self._outbox.put({
            "type": "goal.mode.request",
            "mode": "goal",
            "card_id": card_id,
        })
        return True


def _build_tile(spec: str) -> dict[str, Any]:
    """Turn 'review@0.3.2' or 'review' into a stack tile dict.

    Roles are inferred from the tile id prefix so the manual CLI stays terse:
    'aide*' -> agent, 'project*' -> project, everything else -> skill.
    """
    tile_id, _, revision = spec.partition("@")
    role = "skill"
    if tile_id.startswith("aide"):
        role = "agent"
    elif tile_id.startswith("project"):
        role = "project"
    elif tile_id.startswith("review"):
        role = "review"
    return {
        "tile_id": tile_id,
        "role": role,
        "skill_id": tile_id.split(".", 1)[0],
        "revision": revision or "0.0.1",
    }


def _mock_command(raw_line: str) -> list[dict[str, Any]] | None:
    if not raw_line.startswith("!"):
        return None
    parts = raw_line[1:].strip().split()
    if not parts:
        return []
    command, *args = parts
    context_id = "ctx-mock"
    if command == "help":
        print(HELP_TEXT, flush=True)
        return []
    if command == "compose":
        if args == ["clear"]:
            return [{"type": "tile.stack.state", "context_id": context_id, "stack": []}]
        stack = [_build_tile(spec) for spec in args]
        return [
            {"type": "tile.stack.state", "context_id": context_id, "stack": stack},
            {
                "type": "context.composed",
                "context_id": context_id,
                "skills": [tile["skill_id"] for tile in stack],
                "duration_ms": 640,
                "status": "ok",
            },
        ]
    if command == "event" and args:
        summary = " ".join(args)
        return [
            {
                "type": "task.event",
                "task_id": "runtime-1",
                "event_id": f"e-{abs(hash(summary)) % 10000}",
                "ts": "12:34",
                "summary": summary,
            }
        ]
    if command == "skill" and len(args) >= 2:
        return [
            {
                "type": "skill.updated",
                "skill_id": args[0],
                "revision": args[1],
                "previous": "0.0.0",
                "status": "ready",
                "summary": "manual mock reload",
            }
        ]
    if command == "task" and len(args) >= 3:
        state = args[0]
        try:
            progress = int(args[1])
        except ValueError:
            print(f"Invalid progress: {args[1]}", flush=True)
            return []
        summary = " ".join(args[2:])
        return [
            {
                "type": "task.state",
                "task_id": "runtime-1",
                "state": state,
                "progress": progress,
                "summary": summary,
            }
        ]
    if command == "approval" and args:
        return [
            {
                "type": "approval.request",
                "request_id": "r-mock",
                "summary": " ".join(args),
            }
        ]
    print(f"Unknown mock command: {command}. Try !help.", flush=True)
    return []


def encode_usb_line(message: dict[str, Any]) -> bytes:
    payload = json.dumps(message, separators=(",", ":"))
    return f"{USB_FRAME_PREFIX}{payload}\n".encode("utf-8")


def decode_usb_line(line: str) -> dict[str, Any] | None:
    if not line.startswith(USB_FRAME_PREFIX):
        return None
    try:
        message = json.loads(line[len(USB_FRAME_PREFIX):])
    except json.JSONDecodeError:
        return None
    return message if isinstance(message, dict) else None


def send_json(connection: socket.socket, message: dict[str, Any]) -> None:
    if isinstance(connection, UsbConnection):
        payload = encode_usb_line(message)
        display_payload = payload.decode("utf-8").rstrip()
    else:
        payload = (json.dumps(message, separators=(",", ":")) + "\n").encode("utf-8")
        display_payload = payload.decode("utf-8").rstrip()
    connection.sendall(payload)
    print(f"> {display_payload}", flush=True)


class UsbConnection:
    def __init__(self, path: str) -> None:
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self._saved_attrs = termios.tcgetattr(self.fd)
        tty.setraw(self.fd)
        self._buffer = bytearray()

    def __enter__(self) -> "UsbConnection":
        return self

    def __exit__(self, exc_type: Any, exc_value: Any, traceback: Any) -> None:
        termios.tcsetattr(self.fd, termios.TCSANOW, self._saved_attrs)
        os.close(self.fd)

    def fileno(self) -> int:
        return self.fd

    def sendall(self, payload: bytes) -> None:
        sent = 0
        while sent < len(payload):
            sent += os.write(self.fd, payload[sent:])

    def read_lines(self) -> list[str]:
        try:
            self._buffer.extend(os.read(self.fd, 4096))
        except BlockingIOError:
            return []
        lines = self._buffer.split(b"\n")
        self._buffer = bytearray(lines.pop())
        decoded: list[str] = []
        for raw_line in lines:
            message = decode_usb_line(raw_line.decode("utf-8", errors="replace").rstrip("\r"))
            if message is not None:
                decoded.append(json.dumps(message, separators=(",", ":")))
        return decoded


def _apply_pipeline(connection: Any, pipeline: CodexPipeline | None,
                    raw_line: str) -> None:
    """Feed one device-side frame line through the optional Codex pipeline.

    ``raw_line`` is the JSON payload with the ``@passport `` prefix already
    stripped. Any Passport frame the pipeline emits is forwarded back through
    ``send_json`` so it lands in the same log/serialisation path as manual
    input.
    """
    if pipeline is None:
        return
    try:
        frame = json.loads(raw_line)
    except json.JSONDecodeError:
        return
    if not isinstance(frame, dict):
        return
    for reply in pipeline.dispatch(frame):
        send_json(connection, reply)


def _drain_nfc_outbox(connection: Any,
                      outbox: "queue.Queue[dict[str, Any]] | None") -> None:
    if outbox is None:
        return
    while True:
        try:
            frame = outbox.get_nowait()
        except queue.Empty:
            return
        send_json(connection, frame)


def run(host: str, port: int, pipeline: CodexPipeline | None = None,
        nfc_outbox: "queue.Queue[dict[str, Any]] | None" = None) -> int:
    with socket.create_connection((host, port), timeout=10) as connection:
        connection.setblocking(False)

        print("Connected. Type a JSON message and press Enter; Ctrl-D exits.", flush=True)
        while True:
            _drain_nfc_outbox(connection, nfc_outbox)
            readable, _, _ = select.select([connection, sys.stdin], [], [], 0.25)
            if connection in readable:
                data = connection.recv(4096)
                if not data:
                    print("Passport disconnected.", flush=True)
                    return 0
                for raw_line in data.decode("utf-8", errors="replace").splitlines():
                    print(f"< {raw_line}", flush=True)
                    _apply_pipeline(connection, pipeline, raw_line)
            if sys.stdin in readable:
                raw_line = sys.stdin.readline()
                if not raw_line:
                    return 0
                stripped = raw_line.strip()
                mock_messages = _mock_command(stripped)
                if mock_messages is not None:
                    for message in mock_messages:
                        send_json(connection, message)
                    continue
                try:
                    message = json.loads(raw_line)
                except json.JSONDecodeError as error:
                    print(f"Invalid JSON: {error}", file=sys.stderr, flush=True)
                    continue
                send_json(connection, message)


def run_usb(path: str, pipeline: CodexPipeline | None = None,
            nfc_outbox: "queue.Queue[dict[str, Any]] | None" = None) -> int:
    with UsbConnection(path) as connection:
        print(f"Connected to USB Serial/JTAG {path}. Type a JSON message, or a mock command (!help), and press Enter; Ctrl-D exits.", flush=True)
        while True:
            _drain_nfc_outbox(connection, nfc_outbox)
            readable, _, _ = select.select([sys.stdin], [], [], 0.05)
            for raw_line in connection.read_lines():
                print(f"< {raw_line}", flush=True)
                _apply_pipeline(connection, pipeline, raw_line)
            if sys.stdin in readable:
                raw_line = sys.stdin.readline()
                if not raw_line:
                    return 0
                stripped = raw_line.strip()
                mock_messages = _mock_command(stripped)
                if mock_messages is not None:
                    for message in mock_messages:
                        send_json(connection, message)
                    continue
                try:
                    message = json.loads(raw_line)
                except json.JSONDecodeError as error:
                    print(f"Invalid JSON: {error}", file=sys.stderr, flush=True)
                    continue
                send_json(connection, message)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.4.1")
    parser.add_argument("--port", type=int, default=4242)
    parser.add_argument("--usb", action="store_true", help="use USB Serial/JTAG instead of TCP")
    parser.add_argument("--serial", default="/dev/cu.usbmodem2101", help="USB Serial/JTAG device path")
    parser.add_argument("--codex", action="store_true",
                        help="pipe every device-side @passport frame through "
                             "tools/codex_adapter.py; requires `codex login` "
                             "on the host")
    parser.add_argument("--codex-cwd", default=None,
                        help="working directory Codex sees for this session "
                             "(defaults to the current shell cwd)")
    parser.add_argument("--codex-model", default=None,
                        help="optional Codex model override")
    parser.add_argument("--codex-sandbox", default="read-only",
                        choices=("read-only", "workspace-write",
                                 "danger-full-access"))
    parser.add_argument("--codex-approval-policy", default="on-request",
                        choices=("untrusted", "on-failure", "on-request",
                                 "never"))
    parser.add_argument("--nfc-relay-port", type=int, default=0,
                        help="if non-zero, start an HTTP relay endpoint that "
                             "phones POST NFC UIDs to; sends validated "
                             "goal.mode.request frames to the device")
    parser.add_argument("--nfc-relay-host", default="127.0.0.1",
                        help="bind address for the NFC relay (defaults to "
                             "loopback; use 0.0.0.0 only on a trusted network)")
    args = parser.parse_args()
    pipeline: CodexPipeline | None = None
    relay: NfcRelayServer | None = None
    nfc_outbox: queue.Queue[dict[str, Any]] | None = None
    if args.codex:
        pipeline = CodexPipeline(
            cwd=args.codex_cwd or os.getcwd(),
            model=args.codex_model,
            sandbox=args.codex_sandbox,
            approval_policy=args.codex_approval_policy,
        )
    if args.nfc_relay_port:
        nfc_outbox = queue.Queue()
        relay = NfcRelayServer(args.nfc_relay_host, args.nfc_relay_port,
                               nfc_outbox)
        relay.start()
    try:
        if args.usb:
            return run_usb(args.serial, pipeline=pipeline, nfc_outbox=nfc_outbox)
        return run(args.host, args.port, pipeline=pipeline, nfc_outbox=nfc_outbox)
    except OSError as error:
        print(f"Bridge connection failed: {error}", file=sys.stderr)
        return 1
    finally:
        if pipeline is not None:
            pipeline.close()
        if relay is not None:
            relay.close()


if __name__ == "__main__":
    raise SystemExit(main())
