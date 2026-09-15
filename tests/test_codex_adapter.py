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

    def call_tool(self, name: str, arguments: dict, timeout: float = 60.0):
        self.calls.append((name, arguments))
        try:
            return self._responses[name]
        except KeyError:
            raise AssertionError(f"unexpected codex tool call: {name}")


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


class ApprovalDecisionStubIsHonest(unittest.TestCase):
    def test_decision_returns_bridge_error(self) -> None:
        client = _StubClient({})
        adapter = codex_adapter.CodexAdapter(client, cwd="/tmp/x")

        frames = adapter.handle({"type": "approval.decision",
                                 "request_id": "r-1",
                                 "decision": "approve"})

        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["type"], "bridge.error")
        self.assertEqual(frames[0]["source"], "codex_adapter")
        self.assertIn("approval bridge is stubbed", frames[0]["detail"])


class UnknownFrameIsIgnored(unittest.TestCase):
    def test_unknown_type_is_dropped(self) -> None:
        client = _StubClient({})
        adapter = codex_adapter.CodexAdapter(client, cwd="/tmp/x")
        frames = adapter.handle({"type": "not.a.real.frame"})
        self.assertEqual(frames, [])


if __name__ == "__main__":
    unittest.main()
