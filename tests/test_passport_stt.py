"""Offline tests for host-side Passport voice capture assembly."""

from __future__ import annotations

import base64
import pathlib
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from passport_stt import VoiceTranscriber  # noqa: E402


def _start(request_id: str = "v-1") -> dict:
    return {
        "type": "voice.capture.start",
        "request_id": request_id,
        "sample_rate": 16000,
        "codec": "pcm16",
    }


def _audio(index: int, pcm: bytes, request_id: str = "v-1") -> dict:
    return {
        "type": "voice.capture.audio",
        "request_id": request_id,
        "index": index,
        "pcm_b64": base64.b64encode(pcm).decode("ascii"),
    }


def _stop(request_id: str = "v-1") -> dict:
    return {
        "type": "voice.capture.stop",
        "request_id": request_id,
        "duration_ms": 20,
        "reason": "manual",
        "text": "[voice 2 chunks 20ms peak=20]",
    }


class VoiceTranscriberTests(unittest.TestCase):
    def _transcriber(self, *, max_audio_bytes: int = 640_000
                     ) -> VoiceTranscriber:
        script = (
            "import sys,wave;"
            "w=wave.open(sys.argv[1],'rb');"
            "assert w.getnchannels()==1;"
            "assert w.getsampwidth()==2;"
            "assert w.getframerate()==16000;"
            "assert w.readframes(w.getnframes())==b'\\x01\\x02\\x03\\x04';"
            "print('测试转写')"
        )
        return VoiceTranscriber(
            [sys.executable, "-c", script, "{wav}"],
            max_audio_bytes=max_audio_bytes,
        )

    def test_reassembles_chunks_by_index_and_replaces_placeholder(self) -> None:
        transcriber = self._transcriber()
        transcriber.process(_start())
        transcriber.process(_audio(1, b"\x03\x04"))
        transcriber.process(_audio(0, b"\x01\x02"))

        result = transcriber.process(_stop())

        self.assertEqual(result["text"], "测试转写")
        self.assertIsNone(transcriber.take_error())

    def test_failed_command_preserves_device_diagnostic(self) -> None:
        transcriber = VoiceTranscriber(
            [sys.executable, "-c", "import sys;sys.exit(7)", "{wav}"])
        transcriber.process(_start())
        transcriber.process(_audio(0, b"\x01\x02"))

        original = _stop()
        result = transcriber.process(original)

        self.assertIs(result, original)
        self.assertIn("exited 7", transcriber.take_error() or "")

    def test_invalid_or_oversized_audio_is_not_transcribed(self) -> None:
        transcriber = self._transcriber(max_audio_bytes=2)
        transcriber.process(_start())
        transcriber.process(_audio(0, b"\x01\x02\x03\x04"))

        original = _stop()
        self.assertIs(transcriber.process(original), original)

    def test_unmatched_stop_is_unchanged(self) -> None:
        transcriber = self._transcriber()
        original = _stop("v-missing")
        self.assertIs(transcriber.process(original), original)

    def test_command_requires_wav_placeholder(self) -> None:
        with self.assertRaises(ValueError):
            VoiceTranscriber(["transcribe"])


if __name__ == "__main__":
    unittest.main()
