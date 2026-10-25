# Cross-build environment for OpenXcom on the New 3DS.
#   docker build -t openxcom-3ds .
#   docker run --rm -v "$PWD:/work" openxcom-3ds ./build-3ds.sh
FROM devkitpro/devkitarm:latest
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential git cmake pkg-config python3 curl ca-certificates && rm -rf /var/lib/apt/lists/*
RUN dkp-pacman -Sy --noconfirm 3ds-dev dkp-toolchain-vars 3ds-sdl 3ds-sdl_mixer 3ds-sdl_image \
        3ds-libpng 3ds-zlib 3ds-libogg 3ds-libvorbisidec 3ds-mikmod && dkp-pacman -Scc --noconfirm
ENV DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
COPY tools/build-portlibs.sh /tmp/build-portlibs.sh
RUN bash /tmp/build-portlibs.sh
WORKDIR /work
