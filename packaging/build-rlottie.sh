#!/usr/bin/env bash
# Fetch and build rlottie as a static library into third_party/rlottie.
# petto links this statically so the .deb has no librlottie runtime dependency.
set -euo pipefail
cd "$(dirname "$0")/.."

VENDOR="third_party/rlottie"
LIB="$VENDOR/librlottie.a"
INC="$VENDOR/include/rlottie_capi.h"

if [ -f "$LIB" ] && [ -f "$INC" ]; then
  echo "rlottie already vendored at $VENDOR"
  exit 0
fi

RLOTTIE_REF="${RLOTTIE_REF:-master}"
SRC="$(mktemp -d)"
trap 'rm -rf "$SRC"' EXIT

echo "Cloning rlottie ($RLOTTIE_REF)..."
git clone --depth 1 --branch "$RLOTTIE_REF" https://github.com/Samsung/rlottie "$SRC" 2>/dev/null \
  || git clone --depth 1 https://github.com/Samsung/rlottie "$SRC"

cmake -S "$SRC" -B "$SRC/build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DLOTTIE_MODULE=OFF \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build "$SRC/build" -j"$(nproc)" --target rlottie

mkdir -p "$VENDOR/include"
cp "$SRC/build/librlottie.a" "$LIB"
cp "$SRC/inc/rlottie_capi.h" "$VENDOR/include/"
cp "$SRC/inc/rlottiecommon.h" "$VENDOR/include/"
echo "Vendored rlottie -> $VENDOR"
