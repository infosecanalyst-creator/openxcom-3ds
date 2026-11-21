#!/usr/bin/env bash
# Clone OpenXcom (first run), apply the 3DS patch, build openxcom.3dsx.
# By default the game data is NOT built in: copy your own X-COM folders to
# sdmc:/3ds/openxcom/UFO/ on the card. BAKE_DATA=1 with your data in ./xcom-data/
# builds it into romfs instead; such a .3dsx contains the game and must never be shared.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# Port version (this repo, not the OpenXcom pin) for the main menu, taken before anything is built.
PORT_VERSION="$(git -c safe.directory='*' -C "$HERE" describe --always --dirty 2>/dev/null || echo unknown)"
SRC="${OPENXCOM_DIR:-$HERE/OpenXcom}"
: "${DEVKITPRO:?DEVKITPRO is not set}"
# OX-05: build the pinned upstream commit (patches/openxcom-commit.txt), never a moving master
PIN="$(tr -d '[:space:]' < "$HERE/patches/openxcom-commit.txt")"
if [ ! -d "$SRC/.git" ]; then
  git init -q "$SRC" && git -C "$SRC" remote add origin https://github.com/OpenXcom/OpenXcom.git
  git -C "$SRC" fetch -q --depth 1 origin "$PIN" && git -C "$SRC" checkout -q FETCH_HEAD
fi
python3 "$HERE/tools/patch_openxcom.py" "$SRC"
if [ "${BAKE_DATA:-0}" = 1 ] && [ -d "$HERE/xcom-data/GEODATA" ]; then
  echo "BAKE_DATA=1: building your game data into the .3dsx. Do not share this file." >&2 mkdir -p "$SRC/bin/UFO" && cp -r "$HERE/xcom-data"/* "$SRC/bin/UFO/"; fi
rm -f "$SRC/src/git_version.h"   # always regenerate the version line
make -C "$SRC/src" -f Makefile.3ds PORT_VERSION="$PORT_VERSION" -j"$(nproc)" 2>&1 | tee "$HERE/build-3ds.log"
# the .3dsx lives directly in /3ds on the card (sdmc:/3ds/openxcom.3dsx); data stays in /3ds/openxcom/
mkdir -p "$HERE/out/3ds" && cp "$SRC/openxcom.3dsx" "$HERE/out/3ds/"
cp "$SRC/openxcom.elf" "$HERE/out/openxcom.elf" 2>/dev/null || true   # for crash addresses
echo "done: out/3ds/openxcom.3dsx"
