<p align="right">
  <strong>简体中文</strong> · <a href="build-and-test.md">English</a>
</p>

# 构建与验证（Build & Test）

使用 ESP-IDF 5.5.3。全新机器或缺少工具链时，先按
[环境引导](environment-setup.zh_CN.md)完成安装。

> 固件编译优先运行 `./tools/validate.sh --firmware`。空白设备初始化或有意完整
> 刷新时，把验证通过的 `build/FoloToy-AI-Passport-full.bin` 从 `0x0` 写入；
> 合并镜像可能重置 NVS，需要保留已有 NVS 状态时使用分段 `idf.py flash`。
> `idf.py build` 和
> `idf.py flash` 只作为增量开发命令，不作为默认交付方式。

```bash
source <ESP-IDF-v5.5.3-路径>/export.sh
idf.py --version             # 必须输出 ESP-IDF v5.5.3
./tools/validate.sh --firmware # 优先：编译并验证 0x0 合并固件
idf.py set-target esp32c3     # 配置目标芯片（fresh checkout 后/换 target 后运行）
idf.py build                  # 可选：增量 app 编译
idf.py flash monitor          # 可选：增量 app 烧录
idf.py fullclean              # 只清空过期生成状态（勿用于清理用户源码改动）
```

`idf.py fullclean` 不能让已有 `sdkconfig` 完整同步变更后的 defaults。需要重建
target 或已跟踪 defaults 时，先保留有意的本地设置，再运行
`idf.py set-target esp32c3`。

仓库提交 `dependencies.lock` 以固定 ESP-IDF Managed Components 的解析结果。修改 `idf_component.yml` 后必须使用 ESP-IDF 5.5.3 重新生成锁文件、review 版本变化并与 manifest 一起提交；普通构建不应产生未提交的锁文件差异。

固件门禁使用全新的临时构建目录，并从仓库 `sdkconfig.defaults` 生成隔离的 `sdkconfig`。它不会读取或覆盖开发者根目录的 `sdkconfig`，只把验证通过的合并镜像复制到 `build/FoloToy-AI-Passport-full.bin`。门禁同时验证[当前配置的固件布局](firmware-layout.zh_CN.md)：从 `flash_args` 读取镜像偏移，检查分区表 MD5、边界和不重叠，并确认应用从所配置的 app 分区起点开始且未超出分区。允许用户自定义分区布局。

当前基线含一个可独立运行的纯逻辑测试：

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_ui_pixel_math.c main/ui_pixel_math.c \
  -o /tmp/test_ui_pixel_math
/tmp/test_ui_pixel_math
```

统一验证入口：

```bash
./tools/validate.sh --static    # 仓库一致性、workflow、文档链接、敏感信息、host tests
./tools/validate.sh --firmware  # ESP-IDF build、merge-bin、偏移与当前布局校验
./tools/validate.sh             # 完整验证
./tools/validate.sh --preflash  # 每次实机烧录前强制执行，见下节
```

完整验证要求预先激活 ESP-IDF 5.5.3。CI 与本地使用同一脚本；若 CI 和本地行为不同，应先修复脚本或环境，而不是维护两份命令。

涉及物理外设的改动必须在真机运行硬件指南验收清单，并把"编译通过"与"硬件验证通过"分开记录。

## 烧录前门禁（Preflash Gate）

任何实机烧录（空板初始化、完整刷新、硬件验收）前都必须先跑：

```bash
./tools/validate.sh --preflash
```

`--preflash` 会先执行 `--static` + `--firmware`，再校验
`build/FoloToy-AI-Passport-full.bin` 是"本次运行"产出的（mtime 不早于脚本
启动时间），杜绝"昨天看着还行"这种拿旧固件烧录的坑。当发现根 `build/`
里还残留一份旧的 `FoloToy-AI-Passport.bin` 时，门禁会打印警告，因为
`idf.py flash` 会静默用那份旧 app 覆盖设备。

成功时，门禁打印的 `esptool.py` 命令里的 `--flash_mode` / `--flash_freq` /
`--flash_size` 来自本次固件构建的 `flasher_args.json`，保证与验证过的
固件镜像一字节不差，不会退化到 esptool 的默认值。

以下任何一项不过都会拒绝签发烧录许可：

- **回归静态断言**——Slice F 审出的 3 个 Major 缺陷不能复活：
  `disconnected_banner` 死字段不得再被引用；
  `demo_passport_service_stop` 在 LVGL 锁超时分支必须仍调用
  `passport_transport_usb_stop()`；两个 demo 不允许直接调用
  `usb_serial_jtag_driver_(un)install`，必须走引用计数化的
  `passport_transport_usb_start/stop`。
- **CJK 字体覆盖**——`main/fonts/ui_cn_16.c` 的 `--symbols` 子集必须
  覆盖所有 include 了 `ui_cn_16.h` 的源文件中出现的汉字（自动发现）。
- **阈值常量**——30 s 断线阈值、60 s 审批超时只能以命名常量存放在
  `main/passport_service.h`；`main/*.c` 中出现裸 `30000` / `60000` 直接失败。
- **固件布局**——8 MB Flash、MD5 分区表、应用装得下配置的 app 分区、
  合并镜像从 `0x0` 起始。

社区只能上传验证通过的 `build/FoloToy-AI-Passport-full.bin`，不得上传应用单镜像
`build/FoloToy-AI-Passport.bin`，后者不包含完整且经校验的固件布局。

## CJK 字体覆盖门禁

`./tools/validate.sh`（`--static` 与 `--firmware` 都会跑）会解析
`main/fonts/ui_cn_16.c` 头部的 `--symbols` 注释，与所有 include
`ui_cn_16.h` 的 `main/*.c` / `main/*.h`（外加共享字符串生成器
`main/passport_ui_model.[ch]`）中出现的 `0x4E00-0x9FFF` 字符做差集。任何 UI
里用到、字体子集里缺失的字符会让门禁直接失败，并输出：

```
CJK glyphs missing from ui_cn_16 subset: ...
Run ./tools/gen_cjk_font.sh to rebuild the font.
```

缺字在实机上表现为空白方块，代码 review 很难发现却会强制硬件重试，因此
放在构建门禁而不是实机验收里。修改 UI 文案后，要么把新字加入
`tools/collect_ui_glyphs.py` 的手工池，要么直接跑
`./tools/gen_cjk_font.sh` 从 `fonts/source/AlibabaPuHuiTi-Regular.ttf`
重新生成 `main/fonts/ui_cn_16.c`，门禁即可通过。
