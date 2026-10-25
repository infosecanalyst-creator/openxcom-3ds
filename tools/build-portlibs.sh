#!/usr/bin/env bash
# Cross-build the two OpenXcom deps devkitPro does not package: SDL_gfx and
# yaml-cpp. Run inside the devkitpro/devkitarm image after `dkp-pacman -S 3ds-sdl`.
set -euo pipefail
: "${DEVKITPRO:?}"; export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
export PORTLIBS="$DEVKITPRO/portlibs/3ds"
source "$DEVKITPRO/3dsvars.sh"   # sets CC/CXX/CFLAGS/PKG_CONFIG for the 3DS
WORK="$(mktemp -d)"; cd "$WORK"

# SDL_gfx 2.0.26 (C, autotools)
curl -L -o sdlgfx.tgz https://sourceforge.net/projects/sdlgfx/files/SDL_gfx-2.0.26.tar.gz/download
tar xzf sdlgfx.tgz && cd SDL_gfx-2.0.26
./configure --prefix="$PORTLIBS" --host=arm-none-eabi --disable-shared --enable-static --disable-mmx \
            --with-sdl-prefix="$PORTLIBS"
make -j"$(nproc)" && make install
cd "$WORK"

# yaml-cpp 0.7 (header-only API, static lib)
git clone --depth 1 --branch yaml-cpp-0.7.0 https://github.com/jbeder/yaml-cpp.git
mkdir yaml-build && cd yaml-build
cmake ../yaml-cpp -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_SYSTEM_PROCESSOR=arm \
      -DCMAKE_C_COMPILER=arm-none-eabi-gcc -DCMAKE_CXX_COMPILER=arm-none-eabi-g++ \
      -DCMAKE_C_FLAGS="$CFLAGS" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
      -DCMAKE_INSTALL_PREFIX="$PORTLIBS" -DYAML_CPP_BUILD_TESTS=OFF -DYAML_CPP_BUILD_TOOLS=OFF \
      -DYAML_CPP_BUILD_CONTRIB=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
make -j"$(nproc)" && make install
echo "portlibs ready in $PORTLIBS"
