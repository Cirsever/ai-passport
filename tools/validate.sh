#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware|--preflash]" >&2
    echo "  --preflash: run --all and confirm build/FoloToy-AI-Passport-full.bin" >&2
    echo "              is fresh (built by this run). Required before every flash." >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_app_startup.c main/app_startup.c \
        -o "${test_dir}/test_app_startup"
    "${test_dir}/test_app_startup"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_service.c main/passport_service.c \
        -o "${test_dir}/test_passport_service"
    "${test_dir}/test_passport_service"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_v2_state.c main/passport_v2_state.c \
        -o "${test_dir}/test_passport_v2_state"
    "${test_dir}/test_passport_v2_state"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_service_demo.c main/passport_service.c \
        -o "${test_dir}/test_passport_service_demo"
    "${test_dir}/test_passport_service_demo"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_ui_model.c main/passport_ui_model.c main/passport_service.c \
        -o "${test_dir}/test_passport_ui_model"
    "${test_dir}/test_passport_ui_model"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_scene.c main/passport_scene.c main/passport_service.c \
        -o "${test_dir}/test_passport_scene"
    "${test_dir}/test_passport_scene"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_line.c main/passport_line.c \
        -o "${test_dir}/test_passport_line"
    "${test_dir}/test_passport_line"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_usb_frame.c main/passport_usb_frame.c \
        -o "${test_dir}/test_passport_usb_frame"
    "${test_dir}/test_passport_usb_frame"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_service_integration.c main/passport_service.c \
        -o "${test_dir}/test_passport_service_integration"
    "${test_dir}/test_passport_service_integration"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_voice_vad.c main/passport_voice_vad.c \
        -o "${test_dir}/test_passport_voice_vad"
    "${test_dir}/test_passport_voice_vad"
    python3 -c 'from pathlib import Path; compile(Path("tools/passport_bridge.py").read_text(), "tools/passport_bridge.py", "exec")'
    python3 -c 'from pathlib import Path; compile(Path("tools/codex_adapter.py").read_text(), "tools/codex_adapter.py", "exec")'
    python3 -c 'from pathlib import Path; compile(Path("tools/passport_stt.py").read_text(), "tools/passport_stt.py", "exec")'
    python3 -c 'from pathlib import Path; compile(Path("tools/passport_companion.py").read_text(), "tools/passport_companion.py", "exec")'
    python3 -c 'from pathlib import Path; compile(Path("tools/trae_adapter.py").read_text(), "tools/trae_adapter.py", "exec")'
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_codex_adapter.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_bridge_codex_glue.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_trae_adapter.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_bridge_trae_glue.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_stt.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_companion.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_nfc_relay.py
    python3 -c 'from tools import passport_bridge as b; assert b.decode_usb_line("@passport {\"type\":\"device.hello\"}")["type"] == "device.hello"; assert b.decode_usb_line("boot log") is None'
    python3 -c 'from pathlib import Path; s = Path("main/passport_transport_usb.c").read_text(); assert "select(" not in s; assert "usb_serial_jtag_is_driver_installed" not in s'
    python3 -c 'from pathlib import Path; s = Path("tools/passport_bridge.py").read_text(); assert "select.select([connection.fileno(), sys.stdin]" not in s; assert "--demo" not in s; assert "demo_messages" not in s'
    python3 -c 'from pathlib import Path; s = Path("main/demo_passport_service.c").read_text(); assert "load_mock_goal_card" not in s; assert "mock NTAG213" not in s; assert "passport_service_mock_line" not in s'
    python3 -c 'from pathlib import Path; assert "usb_serial_jtag_is_connected" not in Path("main/passport_transport_usb.c").read_text(), "DTR-based connection detection is unreliable across host bridges; keep the transport DTR-free"; assert "passport_transport_usb_connected" not in Path("main/passport_transport_usb.h").read_text(), "passport_transport_usb_connected() was intentionally removed"'
    check_regression_asserts
    check_font_coverage
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" -Wl,--gc-sections \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

check_regression_asserts() {
    # Preflash gate: high-impact regressions caught in review and device tests
    # must not come back. Each assertion is scoped to a well-known file so
    # a legitimate rewrite has to consciously touch this list to pass.
    python3 - <<'PY'
import pathlib, re, sys
root = pathlib.Path(".")

# 1. disconnected_banner is a dead field in the current model: no writer, no
# reader anywhere. Guard against the "half-alive" state where the header stops
# writing but demo still reads (or vice versa).
sources = {
    "main/passport_ui_model.h": root/"main/passport_ui_model.h",
    "main/passport_ui_model.c": root/"main/passport_ui_model.c",
    "main/demo_passport_service.c": root/"main/demo_passport_service.c",
}
offenders = []
for label, path in sources.items():
    if "disconnected_banner" in path.read_text(encoding="utf-8"):
        offenders.append(label)
if offenders:
    sys.exit(
        "disconnected_banner is retired; remove references from: "
        + ", ".join(offenders) + "\nSee docs/development/passport-service-status.md")

# 2. demo_passport_service_stop must keep tearing down the transport when
# bsp_lvgl_lock() fails, otherwise the next _start() lands on INVALID_STATE.
stop_text = (root/"main/demo_passport_service.c").read_text(encoding="utf-8")
match = re.search(
    r"demo_passport_service_stop\(void\)\s*\{(.*?)\n\}",
    stop_text, flags=re.DOTALL)
if not match:
    sys.exit("demo_passport_service_stop() not found in demo_passport_service.c")
body = match.group(1)
if "passport_transport_usb_stop()" not in body:
    sys.exit(
        "demo_passport_service_stop() must call passport_transport_usb_stop() "
        "even when the LVGL lock fails")
# The early return pattern `if (!bsp_lvgl_lock(...)) return` is exactly the bug
# the review flagged: refuse it inside this function body.
if re.search(r"if\s*\(\s*!\s*bsp_lvgl_lock\([^\)]*\)\s*\)\s*return\s+", body):
    sys.exit(
        "demo_passport_service_stop() must not early-return on LVGL lock "
        "timeout; call passport_transport_usb_stop() first")

# 3. USB driver ownership: neither demo may skip the ref-counted transport by
# calling usb_serial_jtag_driver_uninstall directly. Only the transport source
# owns the driver.
for label in ("main/demo_passport_service.c", "main/demo_did_tibo_rest.c"):
    text = (root/label).read_text(encoding="utf-8")
    if "usb_serial_jtag_driver_install" in text or \
       "usb_serial_jtag_driver_uninstall" in text:
        sys.exit(
            f"{label} must not touch usb_serial_jtag_driver_(install|uninstall) "
            "directly; go through passport_transport_usb_start/stop so the "
            "reference count is respected")
# The transport itself must keep an explicit ref count so the two demos can
# coexist even if the demo shell only stops one before starting another.
transport = (root/"main/passport_transport_usb.c").read_text(encoding="utf-8")
if "s_ref_count" not in transport:
    sys.exit(
        "main/passport_transport_usb.c must maintain a reference count "
        "(s_ref_count) so shared demos can coexist")

# 4. Push-to-talk contract: the BSP must expose the physical release edge and
# Passport Service must stop an active capture on that edge.
button_bsp = (root/"components/bsp/src/bsp_button.c").read_text(encoding="utf-8")
if "BUTTON_PRESS_END" not in button_bsp or "BSP_BTN_RELEASE" not in button_bsp:
    sys.exit(
        "button BSP must map BUTTON_PRESS_END to BSP_BTN_RELEASE for "
        "push-to-talk release")
if "ev == BSP_BTN_RELEASE" not in stop_text or \
   "passport_voice_worker_manual_stop()" not in stop_text:
    sys.exit(
        "Passport Service must stop active voice capture on BSP_BTN_RELEASE")

# 5. Magic-number budget: 30000/60000 are the Slice F thresholds and must live
# only in passport_service.h as named constants.
for label in ("main/passport_ui_model.c", "main/passport_service.c",
              "main/demo_passport_service.c"):
    text = (root/label).read_text(encoding="utf-8")
    text_no_comments = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text_no_comments = re.sub(r"//.*", "", text_no_comments)
    for magic in ("30000", "60000"):
        if re.search(rf"\b{magic}\b", text_no_comments):
            sys.exit(
                f"{label}: raw literal {magic} found; use "
                "PASSPORT_SERVICE_APPROVAL_TIMEOUT_MS or "
                "PASSPORT_SERVICE_LINK_IDLE_DISCONNECT_MS from passport_service.h")

print("Regression asserts: PASS")
PY
}

check_font_coverage() {
    # Every physical build (host tests or firmware) must confirm that the
    # generated CJK subset in main/fonts/ui_cn_16.c still covers all Chinese
    # glyphs emitted by any UI source that actually renders through ui_cn_16,
    # plus the interactive mock strings from acceptance_slice_f.py and
    # passport_bridge.py which the operator sends to the device. A missing
    # glyph renders as a blank square on the device, which is very cheap to
    # miss during code review but expensive to catch on hardware.
    python3 - <<'PY'
import pathlib, re, sys
root = pathlib.Path(".")
opts = re.search(r"--symbols\s+(\S+)",
                 (root/"main/fonts/ui_cn_16.c").read_text(encoding="utf-8"))
if not opts:
    sys.exit("main/fonts/ui_cn_16.c has no --symbols block")
covered = set(opts.group(1))
# Auto-discover UI sources: anything under main/ that includes ui_cn_16.h,
# plus the string builders they read from (passport_ui_model.c/h). This
# avoids a manual whitelist that goes stale, while ignoring boot/menu
# strings that never reach the CJK renderer.
candidates = list(sorted(root.glob("main/*.c"))) + list(sorted(root.glob("main/*.h")))
seed = {
    root/"main/passport_ui_model.c",
    root/"main/passport_ui_model.h",
    # Acceptance/bridge mock strings render on-device via ! commands during
    # tools/acceptance_slice_f.py. Any CJK they emit that is not in the
    # subset would render as a blank square. Guard the manual flow the same
    # way we guard static UI copy.
    root/"tools/acceptance_slice_f.py",
    root/"tools/passport_bridge.py",
}
sources = set()
for path in candidates:
    if path in seed or 'ui_cn_16.h' in path.read_text(encoding="utf-8"):
        sources.add(path)
# Add the tools/ seeds explicitly (they don't come from the main/ glob above).
for path in seed:
    if path.exists() and path.is_relative_to(root/"tools"):
        sources.add(path)
used = set()
for path in sorted(sources):
    for ch in path.read_text(encoding="utf-8"):
        if 0x4E00 <= ord(ch) <= 0x9FFF:
            used.add(ch)
missing = sorted(used - covered)
if missing:
    sys.exit(
        "CJK glyphs missing from ui_cn_16 subset: " + "".join(missing) +
        "\nRun ./tools/gen_cjk_font.sh to rebuild the font.")
print(f"Font coverage: PASS ({len(used)} glyphs used, {len(covered)} in subset, "
      f"{len(sources)} sources scanned)")
PY
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    # Font coverage is a build-time gate: if new UI strings appear without a
    # regenerated ui_cn_16, the physical device would render blank squares.
    check_font_coverage

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    # Copy flasher_args.json alongside the merged image so preflash can print an
    # esptool command that matches the actual flash mode / freq / size the
    # validated firmware was built with. Without this, the printed command
    # falls back to esptool defaults and would flash a mode-mismatched image.
    if [[ -f "${validation_build_dir}/flasher_args.json" ]]; then
        install -m 0644 \
            "${validation_build_dir}/flasher_args.json" \
            "${repo_root}/build/flasher_args.json"
    fi
    echo "Firmware build: PASS"
)

run_preflash_checks() {
    # Preflash entry: the only supported way to prove the firmware is safe to
    # flash. Requires the merged image to exist and to have been produced by
    # this run so an operator cannot mistake a stale artifact for a fresh one.
    local merged="${repo_root}/build/FoloToy-AI-Passport-full.bin"
    local flasher_args="${repo_root}/build/flasher_args.json"
    local start_ts
    start_ts="$(date +%s)"

    run_static_checks
    run_firmware_checks

    if [[ ! -f "${merged}" ]]; then
        echo "ERROR: build/FoloToy-AI-Passport-full.bin is missing after firmware build" >&2
        return 1
    fi
    local merged_ts
    merged_ts="$(stat -f %m "${merged}" 2>/dev/null || stat -c %Y "${merged}")"
    if (( merged_ts < start_ts )); then
        echo "ERROR: build/FoloToy-AI-Passport-full.bin is older than this validate.sh run" >&2
        echo "       (timestamp ${merged_ts} < start ${start_ts})" >&2
        return 1
    fi

    # Detect a root-level idf.py flash residue that would ignore the preflash
    # artifact. If a stale FoloToy-AI-Passport.bin sits next to the fresh
    # merged image, warn the operator: `idf.py flash` would prefer that file.
    local stale_app="${repo_root}/build/FoloToy-AI-Passport.bin"
    if [[ -f "${stale_app}" ]]; then
        local stale_ts
        stale_ts="$(stat -f %m "${stale_app}" 2>/dev/null || stat -c %Y "${stale_app}")"
        if (( stale_ts < start_ts )); then
            echo "WARNING: build/FoloToy-AI-Passport.bin is older than this preflash run." >&2
            echo "         Delete it or rebuild with 'idf.py build' before 'idf.py flash'," >&2
            echo "         otherwise the incremental flash path would ship stale bits." >&2
        fi
    fi

    echo "Preflash gate: PASS (${merged} is fresh)"
    echo ""
    python3 - "${flasher_args}" "${merged}" <<'PY'
import json, sys, pathlib
args_path = pathlib.Path(sys.argv[1])
merged = sys.argv[2]
if not args_path.is_file():
    # Firmware build did not emit flasher_args.json; fall back to a documented
    # baseline (dio/80m/8MB) but tell the operator to trust the build output.
    print("Ready to flash. From 0x0 (flasher_args.json missing, using baseline):")
    print(f"  esptool.py --chip esp32c3 -p <PORT> -b 460800 write_flash "
          f"--flash_mode dio --flash_freq 80m --flash_size 8MB 0x0 {merged}")
    sys.exit(0)
args = json.loads(args_path.read_text(encoding="utf-8"))
flash = args.get("flash_settings", {})
mode = flash.get("flash_mode", "dio")
freq = flash.get("flash_freq", "80m")
size = flash.get("flash_size", "8MB")
print("Ready to flash the validated merged image from 0x0:")
print(f"  esptool.py --chip esp32c3 -p <PORT> -b 460800 write_flash \\")
print(f"    --flash_mode {mode} --flash_freq {freq} --flash_size {size} \\")
print(f"    0x0 {merged}")
print("")
print("Notes:")
print("  * The merged image spans bootloader + partition table + app + NVS.")
print("    Flashing it from 0x0 will overwrite NVS on the device.")
print("  * To keep existing NVS, run 'idf.py -p <PORT> flash' instead (writes")
print("    bootloader/partitions/app at their configured offsets and leaves")
print("    NVS alone). Only do this after './tools/validate.sh --preflash'")
print("    has passed in the same session.")
PY
}

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    --preflash)
        run_preflash_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
