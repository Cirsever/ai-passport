#!/usr/bin/env python3
"""Bounded host-side PCM assembly and command-based speech transcription."""

from __future__ import annotations

import base64
import binascii
import pathlib
import subprocess
import tempfile
import wave
from dataclasses import dataclass, field
from typing import Any, Sequence


DEFAULT_MAX_AUDIO_BYTES = 640_000


@dataclass
class _Capture:
    sample_rate: int
    chunks: dict[int, bytes] = field(default_factory=dict)
    total_bytes: int = 0
    rejected: bool = False


class VoiceTranscriber:
    """Collect ``voice.capture.audio`` frames and enrich the matching stop.

    ``command`` is an argv template. Every ``{wav}`` token is replaced with
    the temporary PCM16 mono WAV path. The command must print only the final
    transcript to stdout and return zero on success.
    """

    def __init__(self, command: Sequence[str], *, timeout: float = 60.0,
                 max_audio_bytes: int = DEFAULT_MAX_AUDIO_BYTES) -> None:
        if not command or not any("{wav}" in token for token in command):
            raise ValueError("STT command must contain a {wav} placeholder")
        if timeout <= 0 or max_audio_bytes <= 0:
            raise ValueError("STT timeout and audio limit must be positive")
        self._command = list(command)
        self._timeout = timeout
        self._max_audio_bytes = max_audio_bytes
        self._captures: dict[str, _Capture] = {}
        self.last_error: str | None = None

    def process(self, frame: dict[str, Any]) -> dict[str, Any]:
        frame_type = frame.get("type")
        request_id = frame.get("request_id")
        if not isinstance(request_id, str) or not request_id:
            return frame

        if frame_type == "voice.capture.start":
            sample_rate = frame.get("sample_rate")
            codec = frame.get("codec")
            if (isinstance(sample_rate, int) and 8000 <= sample_rate <= 48000
                    and codec == "pcm16"):
                # Firmware permits one utterance at a time. Clearing an
                # orphaned capture prevents unbounded host memory if its stop
                # frame was lost during a disconnect.
                self._captures.clear()
                self._captures[request_id] = _Capture(sample_rate)
            return frame

        capture = self._captures.get(request_id)
        if capture is None:
            return frame

        if frame_type == "voice.capture.audio":
            self._append_audio(capture, frame)
            return frame
        if frame_type != "voice.capture.stop":
            return frame

        self._captures.pop(request_id, None)
        if capture.rejected or not capture.chunks:
            return frame
        try:
            transcript = self._transcribe(capture)
        except (OSError, RuntimeError, subprocess.SubprocessError,
                ValueError) as error:
            self.last_error = str(error)
            return frame
        if not transcript:
            self.last_error = "STT command returned an empty transcript"
            return frame

        self.last_error = None
        enriched = dict(frame)
        enriched["text"] = transcript
        return enriched

    def take_error(self) -> str | None:
        error = self.last_error
        self.last_error = None
        return error

    def _append_audio(self, capture: _Capture, frame: dict[str, Any]) -> None:
        index = frame.get("index")
        encoded = frame.get("pcm_b64")
        if (capture.rejected or not isinstance(index, int) or index < 0
                or not isinstance(encoded, str) or index in capture.chunks):
            return
        try:
            pcm = base64.b64decode(encoded, validate=True)
        except (binascii.Error, ValueError):
            capture.rejected = True
            return
        if not pcm or len(pcm) % 2 != 0:
            capture.rejected = True
            return
        if capture.total_bytes + len(pcm) > self._max_audio_bytes:
            capture.rejected = True
            capture.chunks.clear()
            return
        capture.chunks[index] = pcm
        capture.total_bytes += len(pcm)

    def _transcribe(self, capture: _Capture) -> str:
        pcm = b"".join(capture.chunks[index]
                       for index in sorted(capture.chunks))
        with tempfile.TemporaryDirectory(prefix="passport-stt-") as temp_dir:
            wav_path = pathlib.Path(temp_dir) / "capture.wav"
            with wave.open(str(wav_path), "wb") as wav_file:
                wav_file.setnchannels(1)
                wav_file.setsampwidth(2)
                wav_file.setframerate(capture.sample_rate)
                wav_file.writeframes(pcm)
            argv = [token.replace("{wav}", str(wav_path))
                    for token in self._command]
            result = subprocess.run(
                argv, capture_output=True, text=True, timeout=self._timeout,
                check=False,
            )
        if result.returncode != 0:
            detail = (result.stderr or result.stdout).strip()[:240]
            raise RuntimeError(
                f"STT command exited {result.returncode}: {detail}")
        return result.stdout.strip()
