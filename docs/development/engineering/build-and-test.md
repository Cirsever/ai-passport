<p align="right">
  <a href="build-and-test.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Build and Test

Use ESP-IDF 5.5.3. On a clean machine or when the toolchain is missing, follow
the [environment bootstrap](environment-setup.md) first.

> Prefer `./tools/validate.sh --firmware` for firmware builds. Flash its
> verified `build/FoloToy-AI-Passport-full.bin` at offset `0x0` for a blank
> device or an intentional complete refresh. The merged image may reset NVS;
> use segmented `idf.py flash` when existing NVS state must be preserved. Treat
> `idf.py build` and `idf.py flash` as incremental development commands, not the
> default delivery path.

```bash
source <path-to-esp-idf-v5.5.3>/export.sh
idf.py --version             # must report ESP-IDF v5.5.3
./tools/validate.sh --firmware # preferred: build and verify merged 0x0 image
idf.py set-target esp32c3     # fresh checkout or changed target
idf.py build                  # optional incremental application build
idf.py flash monitor          # optional incremental application flash
idf.py fullclean              # remove stale generated build state only
```

`idf.py fullclean` does not fully synchronize an existing `sdkconfig` with
changed defaults. Preserve intentional local settings, then run
`idf.py set-target esp32c3` when the target or tracked defaults must be
regenerated.

The tracked `dependencies.lock` pins Managed Component resolution. After changing an `idf_component.yml`, regenerate the lock with ESP-IDF 5.5.3, review version changes, and commit it with the manifest. An ordinary build must not leave an unexplained lock-file diff.

Firmware validation uses a fresh temporary build directory and an isolated `sdkconfig` generated from the tracked defaults. It does not consume or overwrite a developer's root `sdkconfig`, and it copies only the verified merged image to `build/FoloToy-AI-Passport-full.bin`. The gate also validates the [configured firmware layout](firmware-layout.md): image offsets from `flash_args`, partition-table MD5, bounds and non-overlap, and an application that starts in and fits its configured app partition. User-defined partition layouts are allowed.

The baseline also has a hardware-independent logic test:

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_ui_pixel_math.c main/ui_pixel_math.c \
  -o /tmp/test_ui_pixel_math
/tmp/test_ui_pixel_math
```

Use the unified validation entry point:

```bash
./tools/validate.sh --static    # repository checks, workflows, links, secrets, host tests
./tools/validate.sh --firmware  # build, merge-bin, offsets, and configured layout
./tools/validate.sh             # complete gate; requires an activated ESP-IDF environment
```

CI calls the same script. Fix the shared script or environment if local and CI behavior differs; do not duplicate command sequences in workflows.

Hardware-affecting changes must also run the applicable on-device checklist in the hardware guide. Report compilation separately from physical-device validation.

## CJK font coverage gate

`./tools/validate.sh` (both `--static` and `--firmware`) parses the `--symbols`
header baked into `main/fonts/ui_cn_16.c` and diffs it against every
`0x4E00-0x9FFF` character present in `main/passport_ui_model.c` and
`main/demo_passport_service.c`. Any character used by the UI but missing from
the subset fails the gate with the exact list plus:

```
CJK glyphs missing from ui_cn_16 subset: ...
Run ./tools/gen_cjk_font.sh to rebuild the font.
```

Missing glyphs render as empty squares on the physical device, so this is a
build gate rather than a hardware check. When editing UI strings, either add
the new characters to the manual pool inside `tools/collect_ui_glyphs.py` or
just rerun `./tools/gen_cjk_font.sh`; either path regenerates
`main/fonts/ui_cn_16.c` from `fonts/source/AlibabaPuHuiTi-Regular.ttf` and
lets the gate go green.

Never upload the app-only `build/FoloToy-AI-Passport.bin` to the community. Only
the validated `build/FoloToy-AI-Passport-full.bin` contains the complete checked
firmware layout.
