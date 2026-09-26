#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
export PATH="/Applications/CMake.app/Contents/bin:/usr/local/bin:/opt/homebrew/bin:$PATH"
SDK=${TH10_SDK:-iphoneos}
case "$SDK" in
  iphoneos) ARCH=arm64;;
  iphonesimulator) ARCH=$(uname -m);;
  *) echo 'Unsupported Apple SDK' >&2; exit 2;;
esac
BUILD=${TH10_BUILD_DIR:-$ROOT/.local/build-$SDK}
cmake -S "$ROOT/ios" -B "$BUILD" -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$(xcrun --sdk "$SDK" --show-sdk-path)" \
  -DCMAKE_OSX_ARCHITECTURES="$ARCH" -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DTH10_DEPENDENCIES="${TH10_DEPENDENCIES:-$ROOT/.local/dependencies}" \
  -DTH10_ASSETS="${TH10_ASSETS:-$ROOT/.local/assets}" \
  -DTH10_BUILD_NUMBER="${TH10_BUILD_NUMBER:-1}" \
  -DTH10_DIAGNOSTICS="${TH10_DIAGNOSTICS:-OFF}"
cmake --build "$BUILD" --config RelWithDebInfo --target th10 -- -quiet -jobs "${TH10_BUILD_JOBS:-3}"
# Resource-only edits do not relink the executable; refresh them on every run.
cmake -E copy_directory "${TH10_ASSETS:-$ROOT/.local/assets}" "$BUILD/RelWithDebInfo-$SDK/th10.app/assets"
cmake -E copy_directory "$ROOT/ios/licenses" "$BUILD/RelWithDebInfo-$SDK/th10.app/licenses"
echo "Native bundle: $BUILD/RelWithDebInfo-$SDK/th10.app"
