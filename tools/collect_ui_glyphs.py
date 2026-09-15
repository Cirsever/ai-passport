#!/usr/bin/env python3
"""Collect Chinese glyphs needed by the Passport UI model.

Scans main/passport_ui_model.c and main/demo_passport_service.c for CJK
characters, plus a small hand-picked pool of runtime strings that the manual
mock CLI (tools/passport_bridge.py) is expected to emit. Prints a
space-free sorted string ready to hand to lv_font_conv --symbols.

Also prints the count to stderr for a quick sanity check.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

RUNTIME_POOL = (
    # Task / progress narrative from the mock CLI examples.
    "重构任务跨包分析器扫描开始完成运行中进度补丁草稿就绪测试步骤"
    # Approval and event summaries.
    "写入文件个数据结构冲突错误警告"
    # Compose demo tiles / status text.
    "组合切片版本号会话上下文项目主体审查技能"
)


def collect(sources: list[pathlib.Path]) -> str:
    glyphs: set[str] = set()
    for source in sources:
        if not source.exists():
            continue
        text = source.read_text(encoding="utf-8")
        for ch in text:
            code = ord(ch)
            if 0x4E00 <= code <= 0x9FFF or 0x3000 <= code <= 0x303F:
                glyphs.add(ch)
    for ch in RUNTIME_POOL:
        glyphs.add(ch)
    return "".join(sorted(glyphs))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source",
        action="append",
        default=None,
        help="Extra source file(s) to scan (relative to repo root). May repeat.",
    )
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    defaults = [
        root / "main" / "passport_ui_model.c",
        root / "main" / "demo_passport_service.c",
    ]
    extras = [root / rel for rel in (args.source or [])]
    sources = defaults + extras
    if not any(p.exists() for p in sources):
        print("No source files found; nothing to scan.", file=sys.stderr)
        return 1
    glyphs = collect(sources)
    print(glyphs)
    print(f"glyph_count={len(glyphs)}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
