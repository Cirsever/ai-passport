"""Host-side unit tests for tools/trae_adapter.py.

Fully offline: TraeCliClient is stubbed so no real `trae-cn` process is
spawned. Focus: the Passport-frame façade produces the exact frames documented
in docs/development/ide-adapter-decision.md for the fire-and-forget Trae
integration.
"""

from __future__ import annotations

import importlib.util
import pathlib
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "trae_adapter", ROOT / "tools" / "trae_adapter.py")
assert SPEC and SPEC.loader
trae_adapter = importlib.util.module_from_spec(SPEC)
sys.modules["trae_adapter"] = trae_adapter
SPEC.loader.exec_module(trae_adapter)


class _StubClient:
    """Records every send_chat() call; returns canned (rc, stdout, stderr)."""

    def __init__(self, returncode: int = 0, stdout: str = "",
                 stderr: str = "") -> None:
        self._returncode = returncode
        self._stdout = stdout
        self._stderr = stderr
        self.calls: list[dict] = []

    def send_chat(self, prompt: str, cwd: str | None = None,
                  extra_files=None, timeout: float = 15.0):
        self.calls.append({
            "prompt": prompt, "cwd": cwd,
            "extra_files": list(extra_files) if extra_files else [],
            "timeout": timeout,
        })
        return self._returncode, self._stdout, self._stderr


class GoalModeFiresTraeChat(unittest.TestCase):
    def test_emits_goal_state_and_task_running(self) -> None:
        client = _StubClient()
        adapter = trae_adapter.TraeAdapter(client, cwd="/tmp/passport")

        frames = adapter.handle({"type": "goal.mode.request",
                                 "mode": "goal", "card_id": "card-1"})

        # Trae CLI was invoked exactly once with the operator's card baked
        # into the prompt.
        self.assertEqual(len(client.calls), 1)
        self.assertIn("card-1", client.calls[0]["prompt"])
        self.assertEqual(client.calls[0]["cwd"], "/tmp/passport")

        # Passport-side frames match the wire contract Codex uses too.
        types = [f["type"] for f in frames]
        self.assertEqual(types, ["goal.mode.state", "task.state"])
        self.assertEqual(frames[0]["state"], "enabled")
        self.assertEqual(frames[0]["ide"], "trae")
        self.assertEqual(frames[0]["card_id"], "card-1")
        self.assertTrue(frames[0]["session_id"].startswith("trae-"))
        self.assertEqual(frames[1]["state"], "running")
        self.assertEqual(frames[1]["progress"], 0)


class TraeChatFailureSurfacesBridgeError(unittest.TestCase):
    def test_nonzero_exit_yields_bridge_error(self) -> None:
        client = _StubClient(returncode=1, stderr="no trae window open")
        adapter = trae_adapter.TraeAdapter(client, cwd="/tmp/x")

        frames = adapter.handle({"type": "goal.mode.request",
                                 "card_id": "card-1"})

        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["type"], "bridge.error")
        self.assertEqual(frames[0]["source"], "trae_adapter")
        self.assertIn("no trae window open", frames[0]["detail"])


class SecondCardRejected(unittest.TestCase):
    def test_second_card_does_not_reopen_trae(self) -> None:
        client = _StubClient()
        adapter = trae_adapter.TraeAdapter(client, cwd="/tmp/x")
        adapter.handle({"type": "goal.mode.request", "card_id": "card-1"})
        frames = adapter.handle({"type": "goal.mode.request",
                                 "card_id": "card-2"})
        self.assertEqual(frames, [])
        self.assertEqual(len(client.calls), 1)


class UtteranceRelaysToChat(unittest.TestCase):
    def test_voice_capture_stop_reuses_active_session(self) -> None:
        client = _StubClient()
        adapter = trae_adapter.TraeAdapter(client, cwd="/tmp/x")
        adapter.handle({"type": "goal.mode.request", "card_id": "card-1"})

        frames = adapter.handle({
            "type": "voice.capture.stop",
            "request_id": "v-1",
            "text": "帮我看看这段代码",
        })

        # Second call goes to trae chat with the operator's utterance
        # (Chinese payload traveled unchanged through the adapter).
        self.assertEqual(len(client.calls), 2)
        self.assertEqual(client.calls[-1]["prompt"], "帮我看看这段代码")

        types = [f["type"] for f in frames]
        self.assertEqual(types, ["task.event"])
        self.assertIn("已投递到 Trae Chat", frames[0]["summary"])


class UtteranceWithoutSessionIsDropped(unittest.TestCase):
    def test_no_active_session_yields_no_call(self) -> None:
        client = _StubClient()
        adapter = trae_adapter.TraeAdapter(client, cwd="/tmp/x")

        frames = adapter.handle({"type": "voice.capture.stop",
                                 "request_id": "v-1", "text": "hi"})

        self.assertEqual(frames, [])
        self.assertEqual(client.calls, [])


class ApprovalDecisionSurfacesUnsupported(unittest.TestCase):
    def test_trae_has_no_approval_channel(self) -> None:
        client = _StubClient()
        adapter = trae_adapter.TraeAdapter(client, cwd="/tmp/x")

        frames = adapter.handle({"type": "approval.decision",
                                 "request_id": "r-1",
                                 "decision": "approve"})

        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["type"], "bridge.error")
        self.assertIn("no approval channel", frames[0]["detail"])


class DrainApprovalsIsNoOp(unittest.TestCase):
    def test_drain_pending_approvals_returns_zero(self) -> None:
        adapter = trae_adapter.TraeAdapter(_StubClient(), cwd="/tmp/x")
        emitted: list[dict] = []
        n = adapter.drain_pending_approvals(emitted.append)
        self.assertEqual(n, 0)
        self.assertEqual(emitted, [])


class UnknownFrameIsIgnored(unittest.TestCase):
    def test_unknown_type_is_dropped(self) -> None:
        adapter = trae_adapter.TraeAdapter(_StubClient(), cwd="/tmp/x")
        frames = adapter.handle({"type": "not.a.real.frame"})
        self.assertEqual(frames, [])


class BinaryResolverPrefersPathHits(unittest.TestCase):
    def test_explicit_binary_wins(self) -> None:
        self.assertEqual(trae_adapter._resolve_binary("/usr/local/bin/trae"),
                         "/usr/local/bin/trae")

    def test_missing_binary_raises(self) -> None:
        # Temporarily hide all real candidates.
        original = trae_adapter.CANDIDATE_BINARIES
        trae_adapter.CANDIDATE_BINARIES = ("this-binary-does-not-exist-xyz",)
        try:
            with self.assertRaises(RuntimeError) as ctx:
                trae_adapter._resolve_binary(None)
            self.assertIn("Trae CLI not found", str(ctx.exception))
        finally:
            trae_adapter.CANDIDATE_BINARIES = original


class VisibilityHintIsPrintedOnFirstOpen(unittest.TestCase):
    """After the first --trae field demo we discovered that trae-cn opens a
    new window as advertised, but the newly-focused window can stack behind
    the Trae CN window that spawned it — leaving the operator staring at
    their original chat, convinced 'nothing happened'. Print a stderr hint
    so future operators know to hit Mission Control / ⌘` instead of assuming
    the adapter is broken."""

    def test_first_card_prints_hint_to_stderr(self) -> None:
        import io, contextlib
        adapter = trae_adapter.TraeAdapter(_StubClient(), cwd="/tmp/x")
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            adapter.handle({"type": "goal.mode.request", "mode": "goal",
                            "card_id": "card-42"})
        note = stderr.getvalue()
        self.assertIn("[trae-adapter]", note)
        self.assertIn("card-42", note)
        # Reference at least one of the two revealing gestures so the user
        # can act on the hint without leaving the terminal.
        self.assertTrue(("F3" in note) or ("Mission Control" in note)
                        or ("⌘" in note))


if __name__ == "__main__":
    unittest.main()
