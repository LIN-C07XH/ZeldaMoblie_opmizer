#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_DIR="${1:-"$SCRIPT_DIR/../totk-mobile"}"

if [ ! -d "$TARGET_DIR" ]; then
    echo "错误: 未找到目标目录: $TARGET_DIR"
    exit 1
fi

BACKUP_DIR="$TARGET_DIR/.camera_backup"

if [ ! -d "$BACKUP_DIR" ]; then
    echo "错误: 未找到原版备份目录 ($BACKUP_DIR)，无需卸载或未曾安装。"
    exit 1
fi

echo "=================================================="
echo "正在还原原版 totk-mobile 文件..."
echo "=================================================="

[ -f "$BACKUP_DIR/host/input.h" ] && cp "$BACKUP_DIR/host/input.h" "$TARGET_DIR/host/"
[ -f "$BACKUP_DIR/host/input.c" ] && cp "$BACKUP_DIR/host/input.c" "$TARGET_DIR/host/"
[ -f "$BACKUP_DIR/host/graphics.m" ] && cp "$BACKUP_DIR/host/graphics.m" "$TARGET_DIR/host/"
[ -f "$BACKUP_DIR/ios/main.m" ] && cp "$BACKUP_DIR/ios/main.m" "$TARGET_DIR/ios/"

# 清理附加文件
rm -f "$TARGET_DIR/host/camera_controller.h"
rm -f "$TARGET_DIR/host/camera_controller.inc"
rm -f "$TARGET_DIR/host/action_layout.h"
rm -f "$TARGET_DIR/host/action_layout.inc"
rm -f "$TARGET_DIR/host/screen_aspect.h"
rm -f "$TARGET_DIR/host/screen_aspect.inc"
rm -f "$TARGET_DIR/host/action_file_watcher.h"
rm -f "$TARGET_DIR/host/action_file_watcher.inc"
rm -rf "$BACKUP_DIR"

echo "=================================================="
echo "原版文件还原完成！已恢复到覆盖安装前的初始状态。"
echo "=================================================="
