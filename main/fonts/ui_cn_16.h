#pragma once

#include "lvgl.h"

/* 16 px, 4 bpp Chinese-subset font for Passport Service UI labels.
 * Covers ASCII 0x20-0x7E plus the Chinese glyphs currently produced by
 * passport_ui_model.c. Regenerate with tools/gen_cjk_font.sh whenever new
 * Chinese strings appear in the UI model. */
LV_FONT_DECLARE(ui_cn_16);
