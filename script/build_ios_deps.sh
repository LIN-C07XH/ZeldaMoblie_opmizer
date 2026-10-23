#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source script/ios_config.sh
ios_configure
ios_check_sdk
root="$PWD/work/ios/deps"
mkdir -p "$root" work/tmp
export TMPDIR="$PWD/work/tmp/"
if [[ ! -f "$ios_deps_prefix/lib/libvpx.a" ]]; then
archive="$root/libvpx-1.17.0.tar.gz"
if [[ ! -f "$archive" ]]; then
    curl --fail --location --retry 2 --max-time 180 \
        https://github.com/webmproject/libvpx/archive/refs/tags/v1.17.0.tar.gz -o "$archive.part"
    mv "$archive.part" "$archive"
fi
printf '%s  %s\n' 1020f184046187baa2985dbde38e0691f49c44088bca7a1842b0236c6081dc0a "$archive" | shasum -a 256 -c -
if [[ ! -d "$root/libvpx-1.17.0" ]]; then tar -xzf "$archive" -C "$root"; fi
mkdir -p "$root/build-ios$ZELDA_IOS_DEPLOYMENT_TARGET"
(
cd "$root/build-ios$ZELDA_IOS_DEPLOYMENT_TARGET"
export CC="$(xcrun --sdk iphoneos --find clang)" CXX="$(xcrun --sdk iphoneos --find clang++)"
export SDKROOT="$(xcrun --sdk iphoneos --show-sdk-path)"
export CFLAGS="-target arm64-apple-ios$ZELDA_IOS_DEPLOYMENT_TARGET -isysroot $SDKROOT"
export CXXFLAGS="$CFLAGS" LDFLAGS="$CFLAGS" IOS_VERSION_MIN="$ZELDA_IOS_DEPLOYMENT_TARGET"
"$root/libvpx-1.17.0/configure" --target=arm64-darwin-gcc --prefix="$ios_deps_prefix" \
    --disable-examples --disable-tools --disable-docs --disable-unit-tests \
    --disable-shared --enable-static --disable-vp8-encoder --disable-vp9-encoder \
    --enable-vp9-highbitdepth
make -j4
make install
)
fi
if [[ ! -f "$ios_deps_prefix/lib/libopus.a" ]]; then
    archive="$root/opus-1.6.1.tar.gz"
    if [[ ! -f "$archive" ]]; then
        curl --fail --location --retry 2 --max-time 180 \
            https://ftp.osuosl.org/pub/xiph/releases/opus/opus-1.6.1.tar.gz -o "$archive.part"
        mv "$archive.part" "$archive"
    fi
    printf '%s  %s\n' 6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1 "$archive" | shasum -a 256 -c -
    if [[ ! -d "$root/opus-1.6.1" ]]; then tar -xzf "$archive" -C "$root"; fi
    cmake -S "$root/opus-1.6.1" -B "$root/opus-build-ios$ZELDA_IOS_DEPLOYMENT_TARGET" -G 'Unix Makefiles' \
        -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$ZELDA_IOS_DEPLOYMENT_TARGET" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$ios_deps_prefix" \
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF -DBUILD_SHARED_LIBS=OFF
    cmake --build "$root/opus-build-ios$ZELDA_IOS_DEPLOYMENT_TARGET" -j4
    cmake --install "$root/opus-build-ios$ZELDA_IOS_DEPLOYMENT_TARGET"
fi
