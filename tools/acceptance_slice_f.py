#!/usr/bin/env python3
"""Guided visual acceptance for the Slice F Chinese pages.

Each checkpoint prompts the operator; only after the operator presses Enter
does the script push the checkpoint's mock frames. The bridge stays open until
the last checkpoint so the disconnected banner only appears in Checkpoint 0.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from typing import Sequence


def _prompt(prompt: str) -> None:
    print(f"\n>>> {prompt}")
    input("    Press Enter to send this checkpoint's mock frames ...")


def _confirm(what: str) -> None:
    print(f"    Expect: {what}")
    input("    Press Enter after visually confirming ...")


def _push(bridge: subprocess.Popen[str], commands: Sequence[str], gap: float = 0.4) -> None:
    for cmd in commands:
        bridge.stdin.write(cmd + "\n")
        bridge.stdin.flush()
        print(f"    -> {cmd}")
        time.sleep(gap)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", default="/dev/cu.usbmodem2101")
    parser.add_argument("--bridge", default="tools/passport_bridge.py")
    args = parser.parse_args()

    bridge = subprocess.Popen(
        ["python3", "-u", args.bridge, "--usb", "--serial", args.serial],
        stdin=subprocess.PIPE, stdout=sys.stdout, stderr=sys.stdout,
        text=True, bufsize=1,
    )
    try:
        time.sleep(2.5)  # let the bridge attach

        print("\n>>> Checkpoint 0 — disconnected banner (no frames yet).")
        print("    Look at the device now. Within 3 s the screen should show a")
        print("    full-body grey banner '与主机失联 过期起自 N 秒前' with")
        print("    hint '上 重试  下 快照'.")
        input("    Press Enter after visually confirming Checkpoint 0 ...")

        _prompt("Checkpoint 1 — send task.state.")
        _push(bridge, ["!task running 12 分析器开始扫描"])
        _confirm("Wear.HOME with '运行中 12%', body '分析器开始扫描', hint"
                 " contains '说话', banner GONE.")

        _prompt("Checkpoint 2 — send three task.event frames.")
        _push(bridge, [
            "!event 分析器开始扫描",
            "!event 发现 3 处问题",
            "!event 补丁草稿就绪",
        ])
        _confirm("Wear.HOME row '事件 补丁草稿就绪' (newest event on top).")

        _prompt("Checkpoint 3 — send approval.request.")
        _push(bridge, ["!approval 写入 3 个文件"])
        _confirm("Yellow overlay '写入 3 个文件', hint '上 拒绝  确 通过'.")

        print("\n>>> Checkpoint 3.5 — approval 60 s timeout (optional).")
        print("    Do NOT press any device button. Wait ~60 s and watch the")
        print("    overlay disappear on its own. Skip immediately if you like.")
        input("    Press Enter when you're done watching ...")

        _prompt("Checkpoint 4 — send tile.stack.state + context.composed.")
        _push(bridge, ["!compose aide project.aide review@0.3.2"])
        _confirm("Compose page:\n"
                 "        顶栏 '组合  3 张 ...'\n"
                 "        body '> 审查 0.3.2 / 项目 0.0.1 / 主体 0.0.1 /"
                 " 状态 已组合 640 ms / 上下文 ctx-mock'\n"
                 "        hint '上 返回  下 选下  确 打开'.\n"
                 "        Then press DOWN once on device; pointer '>' moves"
                 " to the 项目 row.")

        _prompt("Checkpoint 5 — clear the stack.")
        _push(bridge, ["!compose clear"])
        _confirm("Page returns to Wear.HOME; 顶栏 back to '随身'.")

        print("\nAll checkpoints exercised. If every step matched, Slice F"
              " visual acceptance is DONE.")
    finally:
        try:
            bridge.stdin.close()
        except Exception:
            pass
        try:
            bridge.wait(timeout=5)
        except subprocess.TimeoutExpired:
            bridge.kill()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
