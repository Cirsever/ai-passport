#!/usr/bin/env python3
"""Manual smoke test for tools/codex_adapter.py.

Not part of the automated gate — it consumes real Codex API quota and needs
`codex login` on the host. Invoke it explicitly when validating the Slice C
adapter end-to-end on a workstation:

    python3 tools/manual_codex_smoke.py --cwd $(pwd)

The script opens a Codex session for a fake `card-smoke` NFC event, feeds one
short utterance, and prints every emitted Passport frame. It never touches
the physical device.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

# Reuse the adapter module directly.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import codex_adapter as ca  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cwd", default=os.getcwd())
    parser.add_argument("--model", default=None)
    parser.add_argument("--sandbox", default=ca.DEFAULT_SANDBOX)
    parser.add_argument("--approval-policy", default=ca.DEFAULT_APPROVAL)
    parser.add_argument("--card-id", default="card-smoke")
    parser.add_argument("--utterance",
                        default="Say 'hello passport' in five words or fewer.")
    args = parser.parse_args()

    client = ca.CodexMcpClient(["codex", "mcp-server"])
    print("Starting codex mcp-server ...", file=sys.stderr)
    info = client.start()
    print(f"codex handshake OK (version={info.get('serverInfo', {}).get('version')})",
          file=sys.stderr)

    adapter = ca.CodexAdapter(client, cwd=args.cwd, model=args.model,
                              sandbox=args.sandbox,
                              approval_policy=args.approval_policy)
    try:
        for frame in adapter.handle({"type": "goal.mode.request",
                                     "mode": "goal",
                                     "card_id": args.card_id}):
            print(json.dumps(frame, ensure_ascii=False))
        for frame in adapter.handle({"type": "voice.capture.stop",
                                     "request_id": "smoke-1",
                                     "text": args.utterance}):
            print(json.dumps(frame, ensure_ascii=False))
    finally:
        client.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
