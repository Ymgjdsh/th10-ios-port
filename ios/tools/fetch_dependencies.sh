#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DEST=${TH10_DEPENDENCIES:-$ROOT/.local/dependencies}
mkdir -p "$DEST"
if [ ! -d "$DEST/SDL" ]; then
  ARCHIVE="$DEST/SDL3-3.4.2.tar.gz"
  curl --fail --location --retry 2 https://github.com/libsdl-org/SDL/releases/download/release-3.4.2/SDL3-3.4.2.tar.gz -o "$ARCHIVE"
  printf '%s  %s\n' ef39a2e3f9a8a78296c40da701967dd1b0d0d6e267e483863ce70f8a03b4050c "$ARCHIVE" | shasum -a 256 -c -
  tar -xzf "$ARCHIVE" -C "$DEST"
  mv "$DEST/SDL3-3.4.2" "$DEST/SDL"
fi
if [ ! -d "$DEST/SDL_ttf" ]; then
  git clone --branch release-3.2.2 --depth 1 https://github.com/libsdl-org/SDL_ttf.git "$DEST/SDL_ttf"
fi
test "$(git -C "$DEST/SDL_ttf" rev-parse HEAD)" = a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b
git -C "$DEST/SDL_ttf" submodule update --init --depth 1 external/freetype
test "$(git -C "$DEST/SDL_ttf/external/freetype" rev-parse HEAD)" = 9973564cfa63763a3e4ac67c09147899539b1e07
grep -Eq '^#define SDL_MAJOR_VERSION +3$' "$DEST/SDL/include/SDL3/SDL_version.h"
grep -Eq '^#define SDL_MINOR_VERSION +4$' "$DEST/SDL/include/SDL3/SDL_version.h"
grep -Eq '^#define SDL_MICRO_VERSION +2$' "$DEST/SDL/include/SDL3/SDL_version.h"
echo 'Dependencies ready: SDL 3.4.2, SDL_ttf 3.2.2, FreeType 2.13.2 (pinned SDL fork).'
