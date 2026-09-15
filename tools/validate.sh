#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
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
        tests/test_passport_service_demo.c main/passport_service.c \
        -o "${test_dir}/test_passport_service_demo"
    "${test_dir}/test_passport_service_demo"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_ui_model.c main/passport_ui_model.c main/passport_service.c \
        -o "${test_dir}/test_passport_ui_model"
    "${test_dir}/test_passport_ui_model"
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
    python3 -c 'from pathlib import Path; compile(Path("tools/passport_bridge.py").read_text(), "tools/passport_bridge.py", "exec")'
    python3 -c 'from pathlib import Path; compile(Path("tools/codex_adapter.py").read_text(), "tools/codex_adapter.py", "exec")'
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_codex_adapter.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_bridge_codex_glue.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_nfc_relay.py
python3 -c 'from tools import passport_bridge as b; assert b.decode_usb_line("@passport {\"type\":\"device.hello\"}")["type"] == "device.hello"; assert b.decode_usb_line("boot log") is None'
python3 -c 'from pathlib import Path; s = Path("main/passport_transport_usb.c").read_text(); assert "select(" not in s; assert "usb_serial_jtag_is_driver_installed" not in s'
python3 -c 'from pathlib import Path; s = Path("tools/passport_bridge.py").read_text(); assert "select.select([connection.fileno(), sys.stdin]" not in s; assert "--demo" not in s; assert "demo_messages" not in s'
python3 -c 'from pathlib import Path; s = Path("main/demo_passport_service.c").read_text(); assert "load_mock_goal_card" not in s; assert "mock NTAG213" not in s; assert "passport_service_mock_line" not in s'
    python3 -c 'from pathlib import Path; assert "usb_serial_jtag_is_connected" not in Path("main/passport_transport_usb.c").read_text(), "DTR-based connection detection is unreliable across host bridges; keep the transport DTR-free"; assert "passport_transport_usb_connected" not in Path("main/passport_transport_usb.h").read_text(), "passport_transport_usb_connected() was intentionally removed"'
    check_font_coverage
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

check_font_coverage() {
    # Every physical build (host tests or firmware) must confirm that the
    # generated CJK subset in main/fonts/ui_cn_16.c still covers all Chinese
    # glyphs emitted by the UI. A missing glyph renders as a blank square on
    # the device, which is very cheap to miss during code review but expensive
    # to catch on hardware.
    python3 - <<'PY'
import pathlib, re, sys
root = pathlib.Path(".")
opts = re.search(r"--symbols\s+(\S+)",
                 (root/"main/fonts/ui_cn_16.c").read_text(encoding="utf-8"))
if not opts:
    sys.exit("main/fonts/ui_cn_16.c has no --symbols block")
covered = set(opts.group(1))
sources = [root/"main/passport_ui_model.c",
           root/"main/demo_passport_service.c"]
used = set()
for path in sources:
    if not path.exists():
        continue
    for ch in path.read_text(encoding="utf-8"):
        if 0x4E00 <= ord(ch) <= 0x9FFF:
            used.add(ch)
missing = sorted(used - covered)
if missing:
    sys.exit(
        "CJK glyphs missing from ui_cn_16 subset: " + "".join(missing) +
        "\nRun ./tools/gen_cjk_font.sh to rebuild the font.")
print(f"Font coverage: PASS ({len(used)} glyphs used, {len(covered)} in subset)")
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
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

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
    *)
        usage
        exit 2
        ;;
esac
