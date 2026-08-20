#!/bin/bash
# DOSBox-X Kindle (Paperwhite 2) cross-build script
# Produces a single stripped dosbox-x binary (SDL2 + zlib + libpng + C++ runtime
# statically linked; only glibc + X11 linked dynamically from the device).
#
# Usage: ./build-kindle.sh
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
TOOLCHAIN=/home/tqhyg/x-tools/arm-kindlepw2-linux-gnueabi
TC_BIN="$TOOLCHAIN/bin"
SYSROOT="$TOOLCHAIN/arm-kindlepw2-linux-gnueabi/sysroot"
HOST=arm-kindlepw2-linux-gnueabi
ART="$ROOT/build-artifacts"
DEP_INSTALL="$ART/deps-install"
SDL2_INSTALL="$ART/sdl2-install"

export PATH="$TC_BIN:$PATH"
export CC="$HOST-gcc"
export CXX="$HOST-g++"
export AR="$HOST-ar"
export RANLIB="$HOST-ranlib"
export STRIP="$HOST-strip"
export PKG_CONFIG_PATH="$SDL2_INSTALL/lib/pkgconfig"

mkdir -p "$ART"

# --- 1. Static SDL2 (X11 video, dummy audio) ---
echo "==> Building SDL2 (static)"
mkdir -p "$ART/sdl2-build"
cd "$ART/sdl2-build"
"$ROOT/vs/sdl2/configure" \
    --host="$HOST" \
    --prefix="$SDL2_INSTALL" \
    --enable-static --disable-shared \
    --enable-video-x11 \
    --disable-video-wayland --disable-video-kmsdrm --disable-video-directfb \
    --disable-video-rpi --disable-video-vivante --disable-video-offscreen \
    --disable-video-opengl --disable-video-opengles1 --disable-video-opengles2 --disable-video-vulkan \
    --disable-alsa --disable-pulseaudio --disable-oss --disable-pipewire \
    --disable-jack --disable-esd --disable-arts --disable-sndio --disable-nas \
    --disable-joystick --disable-haptic --disable-rpath
make -j"$(nproc)"
make install

# --- 2. Static zlib + libpng (in-tree, sysroot libpng is inconsistent) ---
echo "==> Building zlib + libpng (static)"
mkdir -p "$DEP_INSTALL" "$ART/zlib-build" "$ART/libpng-build"

cd "$ART/zlib-build"
"$ROOT/vs/zlib/configure" --static --prefix="$DEP_INSTALL"
make -j"$(nproc)"
make install

cd "$ART/libpng-build"
CPPFLAGS="-I$DEP_INSTALL/include" LDFLAGS="-L$DEP_INSTALL/lib" \
"$ROOT/vs/libpng/configure" \
    --host="$HOST" \
    --prefix="$DEP_INSTALL" \
    --disable-shared --enable-static
make -j"$(nproc)"
make install

# --- 3. DOSBox-X ---
echo "==> Configuring DOSBox-X"
cd "$ROOT"
export CPPFLAGS="-D__STDC_FORMAT_MACROS -I$DEP_INSTALL/include"
export LDFLAGS="-L$DEP_INSTALL/lib -static-libgcc -static-libstdc++"
./configure \
    --host="$HOST" \
    --enable-sdl2 \
    --disable-sdl2test \
    --disable-opengl \
    --disable-freetype \
    --disable-sdlnet \
    --disable-alsa-midi \
    --disable-libfluidsynth \
    --disable-libslirp \
    --disable-avcodec \
    --disable-dynamic-x86 \
    --disable-dynrec

# Force static libatomic (only 64-bit atomics need it on 32-bit ARM; it is
# otherwise a GCC runtime .so that may not exist on the device).
sed -i 's/-latomic/-Wl,-Bstatic -latomic -Wl,-Bdynamic/g' Makefile src/Makefile

echo "==> Building DOSBox-X"
make -j"$(nproc)"

echo "==> Stripping and installing to build-artifacts"
"$STRIP" -o "$ART/dosbox-x" src/dosbox-x
echo "Done. Binary: $ART/dosbox-x"
