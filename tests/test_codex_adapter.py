"""Host-side unit tests for tools/codex_adapter.py.

Runs entirely offline: swaps out CodexMcpClient with a stub so no `codex`
CLI is required. Focus: the Passport-frame façade produces the exact frames
promised in docs/development/ide-adapter-decision.md.
"""

from __future__ import annotations

import importlib.util
import pathlib
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "codex_adapter", ROOT / "tools" / "codex_adapter.py")
assert SPEC and SPEC.loader
codex_adapter = importlib.util.module_from_spec(SPEC)
sys.modules["codex_adapter"] = codex_adapter
SPEC.loader.exec_module(codex_adapter)


class _StubClient:
    def __init__(self, responses: dict[str, dict]) -> None:
        self._responses = responses
        self.calls: list[tuple[str, dict]] = []
        # server_requests: queue of dicts the stub replays when the adapter
        # drains approvals. Push in test setup to simulate Codex issuing
        # elicitation/create requests.
        self.server_requests: list[dict] = []
        # responded: (rpc_id, result, error) tuples the adapter sent back via
        # respond_to_server_request(). Tests assert against this.
        self.responded: list[tuple] = []

    def call_tool(self, name: str, arguments: dict, timeout: float = 60.0):
        self.calls.append((name, arguments))
        try:
            return self._responses[name]
        except KeyError:
            raise AssertionError(f"unexpected codex tool call: {name}")

    def pop_server_request(self, timeout: float | None = 0.0):
        if not self.server_requests:
            return None
        return self.server_requests.pop(0)

    def respond_to_server_request(self, request_id, result=None, error=None):
        self.responded.append((request_id, result, error))


class _StubSessionClient:
    def __init__(self) -> None:
        self.calls: list[tuple[str, dict]] = []
        self.server_requests: list[dict] = []
        self.responded: list[tuple] = []
        self.thread = {
            "id": "thread-native-1",
            "name": "Duplicate title",
            "preview": "preview",
            "status": {"type": "idle"},
        }

    def request(self, method: str, params: dict, timeout: float = 30.0):
        self.calls.append((method, params))
        if method == "thread/list":
            second = dict(self.thread, id="thread-native-2")
            return {"data": [self.thread, second], "nextCursor": None}
        if method == "thread/read":
            return {"thread": self.thread}
        if method == "thread/resume":
            return {"thread": self.thread}
        if method == "turn/start":
            return {"turn": {"id": "turn-1", "status": "inProgress"}}
        raise AssertionError(f"unexpected app-server request: {method}")

    def pop_server_request(self, timeout: float | None = 0.0):
        if not self.server_requests:
            return None
        return self.server_requests.pop(0)

    def respond_to_server_request(self, request_id, result=None, error=None):
        self.responded.append((request_id, result, error))


class GoalModeOpensCodexSession(unittest.TestCase):
    def test_emits_goal_enabled_and_running_task(self) -> None:
        client = _StubClient({
            "codex": {"structuredContent": {"threadId": "abcdef123456",
                                              "content": "Session started."}},
        })
        adapter = codex_adapter.CodexAdapter(
            client, cwd="/tmp/passport-test",
            model="gpt-5.2-codex", sandbox="read-only",
            approval_policy="on-request",
        )

        frames = adapter.handle({"type": "goal.mode.request",
                                 "mode": "goal", "card_id": "card-1"})

        # Codex was invoked exactly once with the config from the decision doc.
        self.assertEqual(len(client.calls), 1)
        tool_name, arguments = client.calls[0]
        self.assertEqual(tool_name, "codex")
        self.assertEqual(arguments["cwd"], "/tmp/passport-test")
        self.assertEqual(arguments["sandbox"], "read-only")
        self.assertEqual(arguments["approval-policy"], "on-request")
        self.assertEqual(arguments["model"], "gpt-5.2-codex")
        self.assertIn("card-1", arguments["prompt"])

        # Passport-side frames match the contract in ide-adapter-decision.md.
        types = [f["type"] for f in frames]
        self.assertEqual(types, ["goal.mode.state", "task.state"])
        self.assertEqual(frames[0]["state"], "enabled")
        self.assertEqual(frames[0]["ide"], "codex")
        self.assertEqual(frames[0]["card_id"], "card-1")
        self.assertTrue(frames[0]["session_id"].startswith("codex-"))
        self.assertEqual(frames[1]["state"], "running")
        self.assertIn("Session", frames[1]["summary"])


class SecondCardIsRejected(unittest.TestCase):
    def test_second_card_does_not_open_new_session(self) -> None:
        client = _StubClient({
            "codex": {"structuredContent": {"threadId": "abcdef", "content": "hi"}},
        })
        adapter = codex_adapter.CodexAdapter(client, cwd="/tmp/x")
        adapter.handle({"type": "goal.mode.request", "card_id": "card-1"})
        # Second card must not spawn a second Codex session.
        frames = adapter.handle({"type": "goal.mode.request", "card_id": "card-2"})
        self.assertEqual(frames, [])
        self.assertEqual(len(client.calls), 1)


class UtteranceContinuesConversation(unittest.TestCase):
    def test_voice_capture_stop_calls_codex_reply(self) -> None:
        client = _StubClient({
            "codex": {"structuredContent": {"threadId": "th-1", "content": "hi"}},
            "codex-reply": {"structuredContent": {"threadId": "th-1",
                                                    "content": "About 42% done, tests running."}},
        })
        adapter = codex_adapter.CodexAdapter(client, cwd="/tmp/x")
        adapter.handle({"type": "goal.mode.request", "card_id": "card-1"})

        frames = adapter.handle({
            "type": "voice.capture.stop",
            "request_id": "v-1",
            "text": "how far are we with the refactor?",
        })

        # Second call goes to codex-reply with the same threadId.
        self.assertEqual(client.calls[-1][0], "codex-reply")
        self.assertEqual(client.calls[-1][1]["threadId"], "th-1")
        self.assertEqual(client.calls[-1][1]["prompt"],
                         "how far are we with the refactor?")

        types = [f["type"] for f in frames]
        self.assertEqual(types, ["task.event", "task.state"])
        self.assertIn("42%", frames[0]["summary"])
        self.assertEqual(frames[1]["state"], "done")
        self.assertEqual(frames[1]["progress"], 100)


class UtteranceWithoutSessionIsDropped(unittest.TestCase):
    def test_no_session_yields_no_call(self) -> None:
        client = _StubClient({})
        adapter = codex_adapter.CodexAdapter(client, cwd="/tmp/x")

        frames = adapter.handle({"type": "voice.capture.stop",
                                 "request_id": "v-1", "text": "hello"})

        self.assertEqual(frames, [])
        self.assertEqual(client.calls, [])


class ExactSessionRouting(unittest.TestCase):
    def _selected_adapter(self):
        client = _StubClient({})
        sessions = _StubSessionClient()
        adapter = codex_adapter.CodexAdapter(
            client, cwd="/workspace", session_client=sessions,
            bridge_id="0123456789abcdef")
        hello = adapter.handle({"type": "device.hello", "protocol": 2})
        self.assertEqual(hello[0]["type"], "host.hello")
        self.assertTrue(hello[0]["sessions"])
        catalog = adapter.handle({"type": "session.list", "tx": 1, "page": 0})
        self.assertEqual([frame["type"] for frame in catalog],
                         ["session.catalog", "session.entry", "session.entry"])
        self.assertNotEqual(catalog[1]["sid"], catalog[2]["sid"])
        selected = adapter.handle({
            "type": "session.select", "tx": 2, "sid": catalog[1]["sid"],
        })
        self.assertEqual(selected[0]["type"], "session.selected")
        return adapter, sessions, selected[0]

    def test_duplicate_titles_keep_distinct_opaque_routes(self) -> None:
        _, sessions, selected = self._selected_adapter()
        self.assertEqual(selected["bridge"], "0123456789abcdef")
        self.assertEqual(selected["epoch"], 1)
        self.assertNotEqual(selected["sid"], "thread-native-1")
        methods = [method for method, _ in sessions.calls]
        self.assertIn("thread/read", methods)
        self.assertIn("thread/resume", methods)

    def test_voice_turn_uses_selected_native_thread(self) -> None:
        adapter, sessions, selected = self._selected_adapter()
        frames = adapter.handle({
            "type": "voice.capture.stop",
            "text": "continue",
            "bridge": selected["bridge"],
            "sid": selected["sid"],
            "epoch": selected["epoch"],
        })
        method, params = sessions.calls[-1]
        self.assertEqual(method, "turn/start")
        self.assertEqual(params["threadId"], "thread-native-1")
        self.assertEqual(params["input"], [{"type": "text", "text": "continue"}])
        self.assertEqual(frames[0]["sid"], selected["sid"])

    def test_stale_voice_route_is_rejected(self) -> None:
        adapter, sessions, selected = self._selected_adapter()
        calls_before = len(sessions.calls)
        frames = adapter.handle({
            "type": "voice.capture.stop",
            "text": "wrong target",
            "bridge": selected["bridge"],
            "sid": selected["sid"],
            "epoch": 0,
        })
        self.assertEqual(len(sessions.calls), calls_before)
        self.assertIn("stale", frames[0]["detail"])

    def test_app_server_approval_uses_route_detail_and_receipt(self) -> None:
        adapter, sessions, selected = self._selected_adapter()
        sessions.server_requests.append({
            "jsonrpc": "2.0",
            "id": 77,
            "method": "item/commandExecution/requestApproval",
            "params": {
                "threadId": "thread-native-1",
                "turnId": "turn-1",
                "itemId": "item-1",
                "startedAtMs": 1,
                "command": "idf.py build",
                "cwd": "/workspace",
            },
        })
        approvals: list[dict] = []
        self.assertEqual(
            adapter.drain_app_server_approvals(approvals.append), 1)
        approval = approvals[0]
        self.assertEqual(approval["operation"], "command")
        self.assertEqual(approval["sid"], selected["sid"])

        details = adapter.handle({
            "type": "approval.detail",
            "bridge": selected["bridge"],
            "sid": selected["sid"],
            "epoch": selected["epoch"],
            "request_id": approval["request_id"],
            "page": 0,
        })
        self.assertEqual(details[0]["type"], "approval.page")
        self.assertIn("idf.py build", details[0]["text"])

        receipts = adapter.handle({
            "type": "approval.decision",
            "bridge": selected["bridge"],
            "sid": selected["sid"],
            "epoch": selected["epoch"],
            "request_id": approval["request_id"],
            "decision": "approve",
        })
        self.assertEqual(sessions.responded[0][1], {"decision": "accept"})
        self.assertEqual(receipts[0]["status"], "allowed")


class ApprovalRoundTripsThroughElicitation(unittest.TestCase):
    def test_drain_emits_approval_request_and_decision_replies_rpc(self) -> None:
        client = _StubClient({})
        adapter = codex_adapter.CodexAdapter(client, cwd="/tmp/x")
        # Simulate Codex sending an elicitation/create request for an
        # exec_approval scenario.
        client.server_requests.append({
            "jsonrpc": "2.0",
            "id": 42,
            "method": "elicitation/create",
            "params": {
                "message": "Codex would like to run: rm -rf /tmp/foo",
                "requestedSchema": {"type": "object"},
            },
        })
        emitted: list[dict] = []
        n = adapter.drain_pending_approvals(emitted.append)
        self.assertEqual(n, 1)
        self.assertEqual(len(emitted), 1)
        approval = emitted[0]
        self.assertEqual(approval["type"], "approval.request")
        self.assertTrue(approval["request_id"].startswith("codex-appr-"))
        self.assertIn("rm -rf", approval["summary"])

        # Operator approves via Passport UI; adapter should reply to rpc id 42.
        response_frames = adapter.handle({
            "type": "approval.decision",
            "request_id": approval["request_id"],
            "decision": "approve",
        })
        self.assertEqual(response_frames, [])  # no wire echo, silent success
        self.assertEqual(len(client.responded), 1)
        rpc_id, result, error = client.responded[0]
        self.assertEqual(rpc_id, 42)
        self.assertIsNone(error)
        self.assertEqual(result, {"decision": "approved"})

    def test_reject_maps_to_denied(self) -> None:
        client = _StubClient({})
        adapter = codex_adapter.CodexAdapter(client, cwd="/tmp/x")
        client.server_requests.append({
            "jsonrpc": "2.0", "id": "abc",
            "method": "elicitation/create",
            "params": {"message": "Apply patch to main.c?"},
        })
        emitted: list[dict] = []
        adapter.drain_pending_approvals(emitted.append)
        request_id = emitted[0]["request_id"]

        adapter.handle({
            "type": "approval.decision",
            "request_id": request_id,
            "decision": "reject",
        })
        rpc_id, result, error = client.responded[0]
        self.assertEqual(rpc_id, "abc")
        self.assertEqual(result, {"decision": "denied"})

    def test_unknown_request_id_yields_bridge_error(self) -> None:
        client = _StubClient({})
        adapter = codex_adapter.CodexAdapter(client, cwd="/tmp/x")
        frames = adapter.handle({
            "type": "approval.decision",
            "request_id": "never-registered",
            "decision": "approve",
        })
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["type"], "bridge.error")
        self.assertIn("unknown approval request_id", frames[0]["detail"])
        self.assertEqual(client.responded, [])

    def test_invalid_decision_does_not_deny_or_consume_request(self) -> None:
        client = _StubClient({})
        adapter = codex_adapter.CodexAdapter(client, cwd="/tmp/x")
        client.server_requests.append({
            "jsonrpc": "2.0", "id": 43,
            "method": "elicitation/create",
            "params": {"message": "Apply patch to main.c?"},
        })
        emitted: list[dict] = []
        adapter.drain_pending_approvals(emitted.append)
        request_id = emitted[0]["request_id"]

        frames = adapter.handle({
            "type": "approval.decision",
            "request_id": request_id,
            "decision": "later",
        })

        self.assertEqual(client.responded, [])
        self.assertEqual(frames[0]["type"], "bridge.error")
        self.assertIn("invalid approval decision", frames[0]["detail"])

        adapter.handle({
            "type": "approval.decision",
            "request_id": request_id,
            "decision": "reject",
        })
        self.assertEqual(client.responded[0][0], 43)
        self.assertEqual(client.responded[0][1], {"decision": "denied"})

    def test_unsupported_server_request_refuses_gracefully(self) -> None:
        client = _StubClient({})
        adapter = codex_adapter.CodexAdapter(client, cwd="/tmp/x")
        client.server_requests.append({
            "jsonrpc": "2.0", "id": 7,
            "method": "sampling/createMessage",  # not supported by adapter
            "params": {},
        })
        emitted: list[dict] = []
        n = adapter.drain_pending_approvals(emitted.append)
        self.assertEqual(n, 0)
        self.assertEqual(emitted, [])
        # Should still respond with a JSON-RPC error so Codex is not stuck.
        self.assertEqual(len(client.responded), 1)
        rpc_id, result, error = client.responded[0]
        self.assertEqual(rpc_id, 7)
        self.assertIsNone(result)
        self.assertEqual(error["code"], -32601)


class UnknownFrameIsIgnored(unittest.TestCase):
    def test_unknown_type_is_dropped(self) -> None:
        client = _StubClient({})
        adapter = codex_adapter.CodexAdapter(client, cwd="/tmp/x")
        frames = adapter.handle({"type": "not.a.real.frame"})
        self.assertEqual(frames, [])


if __name__ == "__main__":
    unittest.main()
