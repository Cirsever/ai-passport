<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# UI CJK font

`main/fonts/ui_cn_16.c` is a generated LVGL v9 font covering ASCII `0x20-0x7E`
plus the exact CJK glyphs currently produced by
[`main/passport_ui_model.c`](../../main/passport_ui_model.c). It is committed
so ESP-IDF builds do not need Node.js on hand.

## Regenerate

Regeneration is only needed after `passport_ui_model.c` gains new Chinese
strings. Steps:

1. Once per checkout, place a static-weight Chinese TTF (default: Alibaba
   PuHuiTi Regular) at `fonts/source/AlibabaPuHuiTi-Regular.ttf`. The directory
   is git-ignored. Variable-format OTFs and CJK sub-formats that `opentype.js`
   cannot parse will fail; PuHuiTi Regular has been validated.
2. Run `./tools/gen_cjk_font.sh`. The script:
   - Scans `main/passport_ui_model.c` via `tools/collect_ui_glyphs.py` to
     enumerate CJK code points actually referenced by the UI.
   - Invokes `npx lv_font_conv` at 16 px, 4 bpp, LVGL v9 format.
   - Writes `main/fonts/ui_cn_16.c`.
3. Commit both the regenerated `main/fonts/ui_cn_16.c` and any updated glyph
   list.

The default source font is Alibaba PuHuiTi, released for commercial use under
Alibaba's open licence. Any equivalent CJK TTF works; set `FONT_SRC=<path>`
before invoking the script.

## Do not

- Do not commit the upstream TTF/OTF file; the repository only ships the
  generated bitmap font.
- Do not switch fonts casually. LVGL v9 fonts are ~90 KB of flash at this
  glyph count; a larger source or higher bpp inflates the binary quickly.
