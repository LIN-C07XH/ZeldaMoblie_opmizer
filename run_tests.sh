#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_DIR="${1:-$SCRIPT_DIR/../totk-mobile}"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/camera-tests.XXXXXX")"
trap 'rm -rf "$TEST_DIR"' EXIT
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s "$SCRIPT_DIR/tests" -p test_overlay_package.py -v
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I "$SCRIPT_DIR/host" \
    "$SCRIPT_DIR/tests/test_camera_controller.c" -o "$TEST_DIR/camera"
"$TEST_DIR/camera"
clang -I "$SCRIPT_DIR/host" "$SCRIPT_DIR/tests/test_action_layout.c" -o "$TEST_DIR/layout"
"$TEST_DIR/layout"
clang -Wall -Wextra -Werror -fsanitize=address,undefined -I "$SCRIPT_DIR/host" \
    "$SCRIPT_DIR/tests/test_screen_aspect.c" -o "$TEST_DIR/aspect"
"$TEST_DIR/aspect"
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I "$SCRIPT_DIR/host" \
    "$SCRIPT_DIR/tests/test_equipment_wheel.c" -o "$TEST_DIR/wheel"
"$TEST_DIR/wheel"
clang -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I "$SCRIPT_DIR/host" \
    "$SCRIPT_DIR/tests/test_native_ui.c" -o "$TEST_DIR/native_ui"
"$TEST_DIR/native_ui"
clang -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I "$SCRIPT_DIR/host" -I "$TARGET_DIR/host" -I "$TARGET_DIR/core/host" \
    "$SCRIPT_DIR/tests/test_touch_input.c" "$SCRIPT_DIR/host/input.c" -o "$TEST_DIR/touch_input"
"$TEST_DIR/touch_input"
clang -Wall -Wextra -Werror -fsanitize=address,undefined \
    "$SCRIPT_DIR/tests/test_filesystem.c" "$SCRIPT_DIR/host/filesystem.c" -o "$TEST_DIR/filesystem"
(cd "$TARGET_DIR" && "$TEST_DIR/filesystem")
clang -Wall -Wextra -Werror -fsanitize=address,undefined -I "$TARGET_DIR/core/host" -I "$TARGET_DIR/host" -I "$TARGET_DIR/tests" \
    "$SCRIPT_DIR/tests/test_vibration.c" "$SCRIPT_DIR/host/vibration.c" -o "$TEST_DIR/vibration"
"$TEST_DIR/vibration"
for name in test_gamepad_haptics test_haptic_routing; do
    clang -Wall -Wextra -Werror -fobjc-arc -fsanitize=address,undefined -I "$TARGET_DIR/core/host" -I "$TARGET_DIR/host" \
        "$SCRIPT_DIR/tests/$name.m" "$SCRIPT_DIR/host/vibration.c" \
        -framework Foundation -framework GameController -framework CoreHaptics -o "$TEST_DIR/$name"
    "$TEST_DIR/$name"
done
clang -Wall -Wextra -Werror -fobjc-arc -fsanitize=address,undefined -I "$SCRIPT_DIR/ios" \
    "$SCRIPT_DIR/tests/test_cloud_save_sync.m" "$SCRIPT_DIR/ios/cloud_save_sync.m" \
    -framework Foundation -o "$TEST_DIR/cloud_save"
"$TEST_DIR/cloud_save"
PYTHONPATH="$TARGET_DIR/script${PYTHONPATH:+:$PYTHONPATH}" PYTHONDONTWRITEBYTECODE=1 \
    "$TARGET_DIR/work/venv/bin/python" -m unittest discover -s "$SCRIPT_DIR/tests" -p test_ui_fonts.py -v
PYTHONPATH="$TARGET_DIR/tools${PYTHONPATH:+:$PYTHONPATH}" PYTHONDONTWRITEBYTECODE=1 \
    "$TARGET_DIR/work/venv/bin/python" -m unittest discover -s "$SCRIPT_DIR/tests" -p test_camera_hook.py -v
PYTHONPATH="$TARGET_DIR/tools${PYTHONPATH:+:$PYTHONPATH}" PYTHONDONTWRITEBYTECODE=1 \
    "$TARGET_DIR/work/venv/bin/python" -m unittest discover -s "$SCRIPT_DIR/tests" -p test_native_ui_hook.py -v
