"""Host-side tests for the NFC relay endpoint inside passport_bridge.py.

Bring up a real HTTP server on loopback, then hit it with the exact same
requests the phone-side automation would issue. Verifies:

- valid UID → 202 and one goal.mode.request frame lands in the outbox
- same UID within debounce window → 429, no additional frame
- malformed payload → 400, no frame
- wrong path → 404
"""

from __future__ import annotations

import importlib.util
import json
import pathlib
import queue
import sys
import time
import unittest
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "passport_bridge", ROOT / "tools" / "passport_bridge.py")
assert SPEC and SPEC.loader
passport_bridge = importlib.util.module_from_spec(SPEC)
sys.modules["passport_bridge"] = passport_bridge
SPEC.loader.exec_module(passport_bridge)


def _post_json(url: str, payload: object) -> tuple[int, dict]:
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data, method="POST",
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=2.0) as resp:
            return resp.status, json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8")
        try:
            return exc.code, json.loads(body)
        except json.JSONDecodeError:
            return exc.code, {"raw": body}


class NfcRelayTests(unittest.TestCase):
    def setUp(self) -> None:
        self.outbox: queue.Queue[dict] = queue.Queue()
        # Port 0 = let the kernel pick a free port; we read it off the server.
        self.relay = passport_bridge.NfcRelayServer(
            "127.0.0.1", 0, self.outbox, debounce_window_ms=500)
        self.relay.start()
        self.url = f"http://127.0.0.1:{self.relay._bound_port}/nfc"

    def tearDown(self) -> None:
        self.relay.close()

    def test_valid_uid_produces_goal_mode_request(self) -> None:
        status, body = _post_json(self.url, {"card_id": "card-1"})
        self.assertEqual(status, 202)
        self.assertTrue(body.get("ok"))
        frame = self.outbox.get(timeout=1.0)
        self.assertEqual(frame, {"type": "goal.mode.request",
                                 "mode": "goal", "card_id": "card-1"})

    def test_second_tap_within_window_is_debounced(self) -> None:
        _post_json(self.url, {"card_id": "card-1"})
        status, body = _post_json(self.url, {"card_id": "card-1"})
        self.assertEqual(status, 429)
        self.assertFalse(body.get("ok"))
        # Only one frame ever queued.
        self.outbox.get(timeout=1.0)
        self.assertTrue(self.outbox.empty())

    def test_different_uid_is_accepted(self) -> None:
        _post_json(self.url, {"card_id": "card-1"})
        status, body = _post_json(self.url, {"card_id": "card-2"})
        self.assertEqual(status, 202)
        # Two frames queued in order.
        first = self.outbox.get(timeout=1.0)
        second = self.outbox.get(timeout=1.0)
        self.assertEqual(first["card_id"], "card-1")
        self.assertEqual(second["card_id"], "card-2")

    def test_malformed_uid_is_rejected(self) -> None:
        status, body = _post_json(self.url, {"card_id": "invalid uid!"})
        self.assertEqual(status, 400)
        self.assertFalse(body.get("ok"))
        self.assertTrue(self.outbox.empty())

    def test_unknown_path_is_404(self) -> None:
        req = urllib.request.Request(
            f"http://127.0.0.1:{self.relay._bound_port}/other",
            data=b"{}", method="POST",
            headers={"Content-Type": "application/json"})
        try:
            urllib.request.urlopen(req, timeout=2.0)
            self.fail("expected 404")
        except urllib.error.HTTPError as exc:
            self.assertEqual(exc.code, 404)
        self.assertTrue(self.outbox.empty())


if __name__ == "__main__":
    unittest.main()
