#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "=== 正在编译并运行相机控制器单元测试 ==="
clang -I "${SCRIPT_DIR}/host" "${SCRIPT_DIR}/tests/test_camera_controller.c" -o /tmp/test_camera_controller
/tmp/test_camera_controller

echo ""
echo "=== 正在编译并运行屏幕适配与动态 Action 布局单元测试 ==="
clang -I "${SCRIPT_DIR}/host" "${SCRIPT_DIR}/tests/test_action_layout.c" -o /tmp/test_action_layout
/tmp/test_action_layout

echo ""
echo "=== 所有单元测试验证完毕，全部通过！ ==="
