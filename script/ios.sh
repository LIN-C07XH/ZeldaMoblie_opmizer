#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PYTHONDONTWRITEBYTECODE=1
source script/ios_config.sh
ios_configure
mkdir -p work/ios work/logs/ios work/tmp
export TMPDIR="$PWD/work/tmp/"
bundle=${ZELDA_IOS_BUNDLE:-local.zelda-mobile.ios}
app="$ios_build_dir/Release-iphoneos/ZeldaMobile.app"
case "${1:-build}" in
build)
    ios_check_sdk
    : "${ZELDA_IOS_TEAM:?Set ZELDA_IOS_TEAM to your development team}"
    work/venv/bin/python tools/prepare_modules.py --check
    work/venv/bin/python script/prepare_graphics.py
    work/venv/bin/python script/prepare_ios_shader_support.py
    work/venv/bin/python script/prepare_ios_shader_ir.py
    bash script/build_ios_deps.sh
    work/venv/bin/python "$PWD/core/ios/tools/gen_embed.py" work/patched work/ios/embed.S main subsdk0 sdk
    cmake -S ios -B "$ios_build_dir" -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_DEPLOYMENT_TARGET="$ZELDA_IOS_DEPLOYMENT_TARGET" -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DZELDA_IOS_METAL="$ZELDA_IOS_METAL" -DZELDA_IOS_DEPS_PREFIX="$ios_deps_prefix" \
        -DZELDA_IOS_TEAM="$ZELDA_IOS_TEAM" -DZELDA_IOS_BUNDLE="$bundle"
    xcodebuild -project "$ios_build_dir/ZeldaMobile.xcodeproj" -target ZeldaMobile \
        -configuration Release -sdk iphoneos -jobs 4 -allowProvisioningUpdates \
        -allowProvisioningDeviceRegistration build
    work/venv/bin/python script/prepare_ios_shader_support.py --verify-bundle "$app"
    work/venv/bin/python script/prepare_ios_shader_ir.py --verify-bundle "$app"
    ;;
install)
    : "${ZELDA_IOS_DEVICE:?Set ZELDA_IOS_DEVICE to the target device}"
    work/venv/bin/python script/prepare_ios_shader_support.py --verify-bundle "$app"
    work/venv/bin/python script/prepare_ios_shader_ir.py --verify-bundle "$app"
    xcrun devicectl device install app --device "$ZELDA_IOS_DEVICE" "$app"
    ;;
launch)
    : "${ZELDA_IOS_DEVICE:?Set ZELDA_IOS_DEVICE to the target device}"
    shift
    xcrun devicectl device process launch --device "$ZELDA_IOS_DEVICE" "$bundle" "$@"
    ;;
logs)
    : "${ZELDA_IOS_DEVICE:?Set ZELDA_IOS_DEVICE to the target device}"
    xcrun devicectl device copy from --device "$ZELDA_IOS_DEVICE" --domain-type appDataContainer \
        --domain-identifier "$bundle" --source Documents/boot.log --destination work/logs/ios/boot.log
    ;;
transfer)
    : "${ZELDA_IOS_DEVICE:?Set ZELDA_IOS_DEVICE to the target device}"
    test -x work/ios/transfer-venv/bin/python || { echo 'Prepare work/ios/transfer-venv with requirements/ios-transfer.txt first' >&2; exit 2; }
    mode=${2:-status}
    report="work/logs/ios/transfer-${mode}-$(date +%Y%m%d-%H%M%S)-$$"
    work/venv/bin/python script/run_guarded.py --seconds 2400 --max-gib 0.5 --report "$report-guard.json" -- \
        work/ios/transfer-venv/bin/python script/ios_transfer.py "$mode" --device "$ZELDA_IOS_DEVICE" --report "$report.jsonl" "${@:3}"
    ;;
*) echo 'usage: script/ios.sh [build|install|launch [arguments...]|logs|transfer [status|probe|romfs|shaders|ir|seed|ready] [--no-verify]]' >&2; exit 2;;
esac
