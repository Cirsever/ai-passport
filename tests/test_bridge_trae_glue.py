"""Host-side tests for the passport_bridge ↔ trae_adapter glue layer.

Fully offline: real TraeCliClient is swapped for a stub via
TraePipeline, so no `trae-cn` process is spawned and no USB port is opened.
Coverage mirrors the codex glue tests so both IDE paths keep the same wire
contract shape.
"""

from __future__ import annotations

import importlib.util
import json
import pathlib
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "passport_bridge", ROOT / "tools" / "passport_bridge.py")
assert SPEC and SPEC.loader
passport_bridge = importlib.util.module_from_spec(SPEC)
sys.modules["passport_bridge"] = passport_bridge
SPEC.loader.exec_module(passport_bridge)


class _FakeConnection:
    def __init__(self) -> None:
        self.sent: list[bytes] = []

    def sendall(self, payload: bytes) -> None:
        self.sent.append(payload)


class _FakeTraePipeline:
    """Standin TraePipeline. Records dispatched frames, returns canned replies.
    drain_approvals is intentionally always empty to match the real Trae
    adapter's contract (no scriptable approval callback)."""

    def __init__(self, replies: list[dict]) -> None:
        self._replies = replies
        self.calls: list[dict] = []

    def dispatch(self, frame: dict) -> list[dict]:
        self.calls.append(frame)
        return list(self._replies)

    def drain_approvals(self) -> list[dict]:
        return []


class TraePipelineIsInvokedOnEveryDeviceFrame(unittest.TestCase):
    def test_goal_mode_request_reaches_pipeline_and_replies_go_out(self) -> None:
        conn = _FakeConnection()
        replies = [
            {"type": "goal.mode.state", "mode": "goal", "state": "enabled",
             "card_id": "card-1", "ide": "trae", "session_id": "trae-abcd"},
            {"type": "task.state", "task_id": "trae-abcd",
             "state": "running", "progress": 0,
             "summary": "Trae Chat 已拉起，请在编辑器窗口继续对话"},
        ]
        pipeline = _FakeTraePipeline(replies)

        raw_line = json.dumps({
            "type": "goal.mode.request", "mode": "goal", "card_id": "card-1",
        })
        passport_bridge._apply_pipeline(conn, pipeline, raw_line)

        # Pipeline saw exactly one dispatched frame.
        self.assertEqual(len(pipeline.calls), 1)
        self.assertEqual(pipeline.calls[0]["card_id"], "card-1")

        # Both synthesized frames were serialized back on the wire.
        wire = b"".join(conn.sent).decode("utf-8")
        self.assertIn("goal.mode.state", wire)
        self.assertIn("trae-abcd", wire)
        self.assertIn("task.state", wire)
        # CJK must be UTF-8 (ensure_ascii=False), never \u escapes.
        self.assertIn("Trae Chat 已拉起", wire)
        self.assertNotIn("\\u", wire)


class TraePipelineErrorsAreForwardedAsBridgeFrames(unittest.TestCase):
    def test_bridge_error_reaches_device_wire(self) -> None:
        conn = _FakeConnection()
        pipeline = _FakeTraePipeline([{
            "type": "bridge.error", "source": "trae_adapter",
            "detail": "trae chat exited 1: no trae window open",
        }])

        raw_line = json.dumps({
            "type": "goal.mode.request", "mode": "goal", "card_id": "card-1",
        })
        passport_bridge._apply_pipeline(conn, pipeline, raw_line)

        wire = b"".join(conn.sent).decode("utf-8")
        self.assertIn("bridge.error", wire)
        self.assertIn("trae_adapter", wire)


class TraeAndCodexAreMutuallyExclusive(unittest.TestCase):
    def test_module_exposes_trae_pipeline_class(self) -> None:
        # Sanity check: bridge exports both pipeline types so main() can
        # choose between them at runtime. If someone renames one, this
        # test breaks before the CLI does.
        self.assertTrue(hasattr(passport_bridge, "CodexPipeline"))
        self.assertTrue(hasattr(passport_bridge, "TraePipeline"))


class NfcRelayFrameFlowsThroughPipeline(unittest.TestCase):
    """The NFC relay hands the bridge a goal.mode.request. When a Trae/Codex
    pipeline is active that frame must reach `dispatch()` after nfc.present.
    Sending goal.mode.request directly to
    the device is wrong: the device's line parser rejects it (it's a
    device→host frame in the protocol), and skipping dispatch means the
    IDE window never opens."""

    def test_relay_frame_reaches_pipeline_not_wire(self) -> None:
        import queue
        conn = _FakeConnection()
        replies = [
            {"type": "goal.mode.state", "mode": "goal", "state": "enabled",
             "card_id": "card-1", "ide": "trae", "session_id": "trae-abc"},
        ]
        pipeline = _FakeTraePipeline(replies)
        outbox: "queue.Queue[dict]" = queue.Queue()
        outbox.put({"type": "goal.mode.request", "mode": "goal",
                    "card_id": "card-1"})

        passport_bridge._drain_nfc_outbox(conn, outbox, pipeline=pipeline)

        # Pipeline saw exactly the relay frame.
        self.assertEqual(len(pipeline.calls), 1)
        self.assertEqual(pipeline.calls[0]["type"], "goal.mode.request")

        # Display observation precedes the reply; never forward the request.
        wire = b"".join(conn.sent).decode("utf-8")
        self.assertIn("goal.mode.state", wire)
        self.assertNotIn("goal.mode.request", wire)

    def test_relay_sends_observation_when_no_pipeline(self) -> None:
        # Without --codex / --trae the mock-CLI path still needs the relay
        # frame to reach the device so a developer can observe card events
        # end-to-end.
        import queue
        conn = _FakeConnection()
        outbox: "queue.Queue[dict]" = queue.Queue()
        outbox.put({"type": "goal.mode.request", "card_id": "card-1"})

        passport_bridge._drain_nfc_outbox(conn, outbox, pipeline=None)

        wire = b"".join(conn.sent).decode("utf-8")
        self.assertEqual(json.loads(wire),
                         {"type": "nfc.present", "card_id": "card-1"})

    def test_observation_arrives_before_slow_dispatch(self) -> None:
        import queue
        conn = _FakeConnection()
        observed = []

        class Pipeline:
            def dispatch(inner_self, frame: dict) -> list[dict]:
                observed.append(json.loads(conn.sent[0]))
                self.assertEqual(frame["type"], "goal.mode.request")
                return [{"type": "goal.mode.state", "state": "enabled"}]

        outbox: "queue.Queue[dict]" = queue.Queue()
        outbox.put({"type": "goal.mode.request", "card_id": "card-1"})
        passport_bridge._drain_nfc_outbox(conn, outbox, pipeline=Pipeline())
        self.assertEqual(observed, [{"type": "nfc.present", "card_id": "card-1"}])
        self.assertEqual(len(conn.sent), 2)


if __name__ == "__main__":
    unittest.main()
