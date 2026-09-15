#!/usr/bin/env bash
# Generate the Passport UI CJK-subset LVGL font.
#
# Requirements:
#   - npx / lv_font_conv reachable (auto-fetched via npx)
#   - A Chinese source TTF at fonts/source/AlibabaPuHuiTi-Regular.ttf
#     (any free-for-commercial-use Chinese TTF works; PuHuiTi is the default
#     we validated against. lv_font_conv's FreeType path does not accept
#     variable-format OTFs, so provide a static-weight TTF.)
#
# Output:
#   main/fonts/ui_cn_16.c  (LVGL v9 font in C, ASCII 0x20-0x7E + Chinese subset)

set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

FONT_SRC="${FONT_SRC:-fonts/source/AlibabaPuHuiTi-Regular.ttf}"
OUTPUT_DIR="main/fonts"
OUTPUT_FILE="${OUTPUT_DIR}/ui_cn_16.c"
FONT_SIZE="${FONT_SIZE:-16}"
BPP="${BPP:-4}"

if [[ ! -f "${FONT_SRC}" ]]; then
    echo "ERROR: source font missing at ${FONT_SRC}." >&2
    echo "  Fetch a Chinese TTF/OTF once, e.g. Noto Sans SC or Source Han Sans." >&2
    exit 1
fi

symbols="$(python3 tools/collect_ui_glyphs.py 2>/dev/null)"
if [[ -z "${symbols}" ]]; then
    echo "ERROR: collect_ui_glyphs.py returned no glyphs." >&2
    exit 1
fi

mkdir -p "${OUTPUT_DIR}"

npx --yes lv_font_conv \
    --font "${FONT_SRC}" \
    --size "${FONT_SIZE}" \
    --bpp "${BPP}" \
    --format lvgl \
    --lv-include lvgl.h \
    --lv-font-name "ui_cn_${FONT_SIZE}" \
    --no-compress \
    -r 0x20-0x7E \
    --symbols "${symbols}" \
    -o "${OUTPUT_FILE}"

echo "Wrote ${OUTPUT_FILE} ($(wc -c <"${OUTPUT_FILE}") bytes, $(printf %s "${symbols}" | wc -m) CJK glyphs + ASCII)"
