"""Host-side tests for the passport_bridge ↔ codex_adapter glue layer.

Uses a fake ``connection`` object (records ``sendall`` payloads) and a stub
CodexAdapter injected through the module's CodexPipeline shim, so no real
Codex CLI or USB port is required.
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

    # Match the UsbConnection duck-typing that send_json expects.
    def sendall(self, payload: bytes) -> None:
        self.sent.append(payload)


class _FakePipeline:
    """Standin CodexPipeline. Records dispatched frames, returns canned replies."""

    def __init__(self, replies: list[dict]) -> None:
        self._replies = replies
        self.calls: list[dict] = []

    def dispatch(self, frame: dict) -> list[dict]:
        self.calls.append(frame)
        return list(self._replies)


class PipelineNoneKeepsBehaviorFlat(unittest.TestCase):
    def test_none_pipeline_is_noop(self) -> None:
        conn = _FakeConnection()
        # send_json here forces the "usb" branch by wrapping with UsbConnection?
        # Not required: _apply_pipeline never sends when pipeline is None.
        passport_bridge._apply_pipeline(conn, None,
                                        '{"type":"device.hello","protocol":1}')
        self.assertEqual(conn.sent, [])


class PipelineIsInvokedOnEveryDeviceFrame(unittest.TestCase):
    def test_frames_reach_pipeline_and_replies_are_serialised(self) -> None:
        conn = _FakeConnection()
        replies = [
            {"type": "goal.mode.state", "mode": "goal", "state": "enabled",
             "card_id": "card-1", "ide": "codex", "session_id": "codex-abcd"},
            {"type": "task.state", "task_id": "codex-abcd",
             "state": "running", "progress": 0, "summary": "Session started"},
        ]
        pipeline = _FakePipeline(replies)

        # Simulate a device-side goal.mode.request landing at the bridge.
        raw_line = json.dumps({
            "type": "goal.mode.request", "mode": "goal", "card_id": "card-1"
        })

        # UsbConnection code path relies on isinstance check; use the plain
        # connection path (TCP) by patching isinstance for the fake object.
        passport_bridge._apply_pipeline(conn, pipeline, raw_line)

        # The pipeline saw exactly the device frame we injected.
        self.assertEqual(pipeline.calls, [json.loads(raw_line)])
        # Both replies were serialised as newline-delimited JSON, TCP style.
        self.assertEqual(len(conn.sent), 2)
        parsed = [json.loads(p.decode("utf-8").rstrip()) for p in conn.sent]
        self.assertEqual(parsed, replies)


class NonDictOrGarbageFramesAreDropped(unittest.TestCase):
    def test_bad_json_does_not_crash(self) -> None:
        conn = _FakeConnection()
        pipeline = _FakePipeline([])
        passport_bridge._apply_pipeline(conn, pipeline, "not json")
        passport_bridge._apply_pipeline(conn, pipeline, "42")  # not a dict
        self.assertEqual(pipeline.calls, [])
        self.assertEqual(conn.sent, [])


class VoiceTranscriptionPrecedesAdapterDispatch(unittest.TestCase):
    def test_pipeline_passes_enriched_stop_to_adapter(self) -> None:
        class _Transcriber:
            def process(self, frame: dict) -> dict:
                enriched = dict(frame)
                enriched["text"] = "transcribed speech"
                return enriched

            def take_error(self) -> None:
                return None

        class _Adapter:
            def __init__(self) -> None:
                self.frames: list[dict] = []

            def handle(self, frame: dict) -> list[dict]:
                self.frames.append(frame)
                return []

        pipeline = passport_bridge.CodexPipeline.__new__(
            passport_bridge.CodexPipeline)
        pipeline._transcriber = _Transcriber()
        pipeline._adapter = _Adapter()

        pipeline.dispatch({
            "type": "voice.capture.stop",
            "request_id": "v-1",
            "text": "[voice diagnostic]",
        })

        self.assertEqual(pipeline._adapter.frames[0]["text"],
                         "transcribed speech")


if __name__ == "__main__":
    unittest.main()
