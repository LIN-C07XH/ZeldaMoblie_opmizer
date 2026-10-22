#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_DIR="${1:-"$SCRIPT_DIR/../totk-mobile"}"

if [ ! -d "$TARGET_DIR" ]; then
    echo "错误: 未找到目标目录: $TARGET_DIR"
    echo "用法: ./install.sh [totk-mobile 路径]"
    exit 1
fi

echo "=================================================="
echo "正在安装相机无限加速度控制覆盖层..."
echo "目标目录: $TARGET_DIR"
echo "=================================================="

BACKUP_DIR="$TARGET_DIR/.camera_backup"
mkdir -p "$BACKUP_DIR/host" "$BACKUP_DIR/ios"

# 自动备份原版文件（仅在首次安装时备份，防止覆盖原始备份）
if [ ! -f "$BACKUP_DIR/host/input.c" ]; then
    echo "[备份] 备份原版文件到 $BACKUP_DIR ..."
    [ -f "$TARGET_DIR/host/input.h" ] && cp "$TARGET_DIR/host/input.h" "$BACKUP_DIR/host/"
    [ -f "$TARGET_DIR/host/input.c" ] && cp "$TARGET_DIR/host/input.c" "$BACKUP_DIR/host/"
    [ -f "$TARGET_DIR/host/graphics.m" ] && cp "$TARGET_DIR/host/graphics.m" "$BACKUP_DIR/host/"
    [ -f "$TARGET_DIR/ios/main.m" ] && cp "$TARGET_DIR/ios/main.m" "$BACKUP_DIR/ios/"
fi

# 复制新文件与覆盖层
echo "[安装] 复制相机控制器核心及覆盖层..."
cp "$SCRIPT_DIR/host/camera_controller.h" "$TARGET_DIR/host/"
cp "$SCRIPT_DIR/host/camera_controller.inc" "$TARGET_DIR/host/"
cp "$SCRIPT_DIR/host/action_layout.h" "$TARGET_DIR/host/"
cp "$SCRIPT_DIR/host/action_layout.inc" "$TARGET_DIR/host/"
cp "$SCRIPT_DIR/host/screen_aspect.h" "$TARGET_DIR/host/"
cp "$SCRIPT_DIR/host/screen_aspect.inc" "$TARGET_DIR/host/"
cp "$SCRIPT_DIR/host/action_file_watcher.h" "$TARGET_DIR/host/"
cp "$SCRIPT_DIR/host/action_file_watcher.inc" "$TARGET_DIR/host/"
cp "$SCRIPT_DIR/host/input.h" "$TARGET_DIR/host/"
cp "$SCRIPT_DIR/host/input.c" "$TARGET_DIR/host/"
cp "$SCRIPT_DIR/host/graphics.m" "$TARGET_DIR/host/"
cp "$SCRIPT_DIR/ios/main.m" "$TARGET_DIR/ios/"

echo "=================================================="
echo "安装完成！"
echo "特性已生效:"
echo "  1. macOS 桌面端: 点击窗口捕获鼠标，ESC 键释放光标；"
echo "     鼠标位移接入无限动态加速度算法，提供 PC 原生视角体验。"
echo "  2. iOS / iPad 移动端: 移除右侧虚拟摇杆，右半屏幕完全支持平滑划屏转视角。"
echo "若需还原，可随时运行 ./uninstall.sh 恢复原版文件。"
echo "=================================================="
