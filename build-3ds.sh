#!/usr/bin/env bash
# Clone OpenXcom (first run), apply the 3DS patch, build openxcom.3dsx.
# Put your X-COM data in ./xcom-data/{GEODATA,GEOGRAPH,...} first and it is
# baked into romfs; otherwise copy it to sdmc:/3ds/openxcom/UFO/ on the card.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${OPENXCOM_DIR:-$HERE/OpenXcom}"
: "${DEVKITPRO:?DEVKITPRO is not set}"
[ -d "$SRC/.git" ] || git clone --depth 1 https://github.com/OpenXcom/OpenXcom.git "$SRC"
python3 "$HERE/tools/patch_openxcom.py" "$SRC"
if [ -d "$HERE/xcom-data/GEODATA" ]; then mkdir -p "$SRC/bin/UFO" && cp -r "$HERE/xcom-data"/* "$SRC/bin/UFO/"; fi
make -C "$SRC/src" -f Makefile.3ds -j"$(nproc)" 2>&1 | tee "$HERE/build-3ds.log"
mkdir -p "$HERE/out/3ds/openxcom" && cp "$SRC/openxcom.3dsx" "$HERE/out/3ds/openxcom/"
echo "done: out/3ds/openxcom/openxcom.3dsx"
