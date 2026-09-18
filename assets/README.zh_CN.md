<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

## 图片（images）

### Passport UI v2 设计预览

- 位置：[`images/passport-ui-v2/`](images/passport-ui-v2/)。
- 来源：[`tools/render_passport_design.py`](../tools/render_passport_design.py)
  以程序绘制的原创插图，项目生成的画作沿用仓库[许可](../LICENSE)。
  示例动物为示意，没有复制 Codex 宠物资源。
- 格式：每种语言 24 张 240 × 320 RGB PNG、四张 1568 × 1604 总览、
  电池与伙伴同步图，以及 JSON 清单。整数倍放大保留像素几何。
- 使用：由 [v2 设计文档](../docs/development/passport-pixel-ui-design.zh_CN.md)
  引用，属于设计交付物，不作为固件运行时资源。
- 生成：`python3 tools/render_passport_design.py`，依赖 Pillow，使用已有的
  `fonts/source/AlibabaPuHuiTi-Regular.ttf`，不新增分发字体文件。
  预览用完整源字体，固件落地时另行生成子集。

可复用的源图与生成的显示资产放在 `images/`。

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
