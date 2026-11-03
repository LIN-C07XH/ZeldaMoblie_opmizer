#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${1:?Pass the UDID of an already booted iOS simulator}"
mkdir -p work/tmp
test_dir=$(mktemp -d "$PWD/work/tmp/ios-touch.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT
app="$test_dir/TouchControlsTests.app"
mkdir -p "$app"
python3 - "$app/Info.plist" <<'PY'
import plistlib
import sys
with open(sys.argv[1], 'wb') as out:
    plistlib.dump(dict(CFBundleIdentifier='local.totk.touch-controls-tests',
        CFBundleExecutable='TouchControlsTests', CFBundleName='TouchControlsTests',
        CFBundlePackageType='APPL', CFBundleVersion='1', CFBundleShortVersionString='1',
        UILaunchScreen={}, UISupportedInterfaceOrientations=['UIInterfaceOrientationLandscapeLeft']), out)
PY
sdk=$(xcrun --sdk iphonesimulator --show-sdk-path)
xcrun --sdk iphonesimulator clang -target arm64-apple-ios17.0-simulator -isysroot "$sdk" \
    -fobjc-arc -Wall -Wextra -Werror -I ios tests/test_ios_touch_controls.m \
    -framework UIKit -framework Foundation -framework CoreGraphics -o "$app/TouchControlsTests"
xcrun simctl install "$1" "$app"
xcrun simctl launch --stderr="$test_dir/result.log" "$1" local.totk.touch-controls-tests
for ((attempt=0;attempt<100;attempt++)); do
    if [[ -f "$test_dir/result.log" ]] && rg -q '^PASS: UIKit tracking' "$test_dir/result.log"; then
        cat "$test_dir/result.log"
        exit 0
    fi
    sleep .1
done
cat "$test_dir/result.log" >&2
exit 1
