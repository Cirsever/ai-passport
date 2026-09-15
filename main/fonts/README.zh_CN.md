<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# UI 中文字体

`main/fonts/ui_cn_16.c` 是一份已生成的 LVGL v9 字体，覆盖 ASCII `0x20-0x7E`
以及 [`main/passport_ui_model.c`](../../main/passport_ui_model.c) 当前产出的
所有中文字形。文件随仓库提交，ESP-IDF 构建不需要额外安装 Node.js。

## 重新生成

仅当 `passport_ui_model.c` 出现新的中文字符时才需要重新生成：

1. 每次 clone 后，把一个静态字重（非可变字体）的中文 TTF 放到
   `fonts/source/AlibabaPuHuiTi-Regular.ttf`，该目录已加入 `.gitignore`。
   `lv_font_conv` 使用的 `opentype.js` 不支持可变字体格式，也无法解析部分
   CJK sub-format 的 OTF；已验证 PuHuiTi Regular 可用。
2. 执行 `./tools/gen_cjk_font.sh`，脚本会：
   - 用 `tools/collect_ui_glyphs.py` 扫描 `main/passport_ui_model.c`，
     枚举 UI 实际使用的 CJK 码位；
   - 调用 `npx lv_font_conv`，输出 16 px、4 bpp、LVGL v9 格式；
   - 覆盖 `main/fonts/ui_cn_16.c`。
3. 同一次提交里带上重新生成的 `main/fonts/ui_cn_16.c` 及可能更新的字形清单。

默认字体源是阿里巴巴普惠体（Alibaba PuHuiTi），阿里官方免费商用授权。
任何等效的中文 TTF 都可以替代，调用脚本前设置 `FONT_SRC=<路径>` 即可。

## 禁止

- 禁止把上游 TTF/OTF 提交进仓库，仓库只保留生成后的点阵字体。
- 禁止随意更换字体。在当前字形数量下，LVGL v9 字体大约占 90 KB Flash；
  更大字体或更高 bpp 会显著撑大固件。
