#!/usr/bin/env bash
# Shared by the app and codec builds. Keep deployment targets out of shared caches.
ios_version_at_least() {
    awk -v actual="$1" -v minimum="$2" 'BEGIN {
        split(actual, a, "."); split(minimum, b, ".");
        for (i = 1; i <= 3; i++) {
            if (a[i] + 0 > b[i] + 0) exit 0;
            if (a[i] + 0 < b[i] + 0) exit 1;
        }
        exit 0;
    }'
}

ios_configure() {
    export ZELDA_IOS_METAL=${ZELDA_IOS_METAL:-4}
    case "$ZELDA_IOS_METAL" in
        3) local minimum=17.0 ;;
        4) local minimum=26.0 ;;
        *) echo 'ZELDA_IOS_METAL must be 3 or 4' >&2; return 2 ;;
    esac
    export ZELDA_IOS_DEPLOYMENT_TARGET=${ZELDA_IOS_DEPLOYMENT_TARGET:-$minimum}
    if [[ ! "$ZELDA_IOS_DEPLOYMENT_TARGET" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]]; then
        echo 'ZELDA_IOS_DEPLOYMENT_TARGET must be a version such as 17.0 or 18.0' >&2; return 2
    fi
    if ! ios_version_at_least "$ZELDA_IOS_DEPLOYMENT_TARGET" "$minimum"; then
        echo "Metal $ZELDA_IOS_METAL requires an iOS deployment target of $minimum or later" >&2; return 2
    fi
    ios_build_dir="$PWD/work/ios/build-metal${ZELDA_IOS_METAL}-ios${ZELDA_IOS_DEPLOYMENT_TARGET}"
    ios_deps_prefix="$PWD/work/ios/deps/install-ios${ZELDA_IOS_DEPLOYMENT_TARGET}"
}

ios_check_sdk() {
    local sdk_version minimum_sdk=18.0
    sdk_version=$(xcrun --sdk iphoneos --show-sdk-version) || return
    if [[ "$ZELDA_IOS_METAL" == 4 ]]; then minimum_sdk=26.0; fi
    if ! ios_version_at_least "$sdk_version" "$minimum_sdk"; then
        echo "Metal $ZELDA_IOS_METAL builds require iPhoneOS SDK $minimum_sdk or later; select a newer Xcode with DEVELOPER_DIR" >&2
        return 2
    fi
    if ! ios_version_at_least "$sdk_version" "$ZELDA_IOS_DEPLOYMENT_TARGET"; then
        echo "iPhoneOS SDK $sdk_version is older than target $ZELDA_IOS_DEPLOYMENT_TARGET; select a newer Xcode with DEVELOPER_DIR" >&2
        return 2
    fi
}
