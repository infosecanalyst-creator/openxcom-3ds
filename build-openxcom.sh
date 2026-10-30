#!/usr/bin/env bash
# Build OpenXcom for the 3DS inside devkitPro's MSYS2 (run: bash build-openxcom.sh)
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH
cd "$(dirname "$0")" || exit 1
LOG=$PWD/openxcom-build.log; : > "$LOG"
{
  # 1. unpack sources (first run)
  [ -d yaml-cpp ] || tar xzf yaml-cpp-src.tgz
  [ -d OpenXcom ] || tar xzf openxcom-src.tgz

  # 2. yaml-cpp for the 3DS -> portlibs (once)
  if [ ! -f $DEVKITPRO/portlibs/3ds/lib/libyaml-cpp.a ]; then
    echo "=== building yaml-cpp for 3DS"
    mkdir -p yaml-build && cd yaml-build
    cmake ../yaml-cpp -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$DEVKITPRO/portlibs/3ds \
      -DYAML_CPP_BUILD_TESTS=OFF -DYAML_CPP_BUILD_TOOLS=OFF -DYAML_CPP_BUILD_CONTRIB=OFF \
      -DBUILD_SHARED_LIBS=OFF -DYAML_BUILD_SHARED_LIBS=OFF && make -j"$(nproc)" && make install
    cd ..
  fi
  ls -la $DEVKITPRO/portlibs/3ds/lib/libyaml-cpp.a || { echo "yaml-cpp missing"; exit 1; }

  # 3. X-COM data from Steam into the data folder (baked into romfs)
  SRC="/c/Program Files (x86)/Steam/steamapps/common/XCom UFO Defense/XCOM"
  # only with BAKE_DATA=1: a .3dsx with the data built in must never be shared
  if [ "${BAKE_DATA:-0}" = 1 ] && [ -d "$SRC/GEODATA" ]; then
    for d in GEODATA GEOGRAPH MAPS ROUTES SOUND TERRAIN UFOGRAPH UFOINTRO UNITS; do
      [ -d OpenXcom/bin/UFO/$d ] || cp -r "$SRC/$d" OpenXcom/bin/UFO/
    done
  fi
  ls OpenXcom/bin/UFO

  # 4. the game
  echo "=== building OpenXcom"
  cd OpenXcom/src && make -f Makefile.3ds -j"$(nproc)"
  echo "EXIT: $?"
  ls -la ../openxcom.3dsx
} 2>&1 | tee -a "$LOG"
