#!/bin/bash
# =============================================================================
# DOSBox-X — Kindle (armv7l soft-float) cross-build script
# =============================================================================
# Produces a single stripped `dosbox-x` binary with:
#   * SDL2, zlib, libpng, libstdc++, libgcc, libatomic  -> statically linked
#   * glibc + X11 (libX11/libXrandr/libxkbfile)         -> dynamic (on device)
#
# The Kindle X server is a minimal Xorg that:
#   * exposes an 8-bit grayscale default visual (e-ink),
#   * does not support X locales,
#   * renders depth-32 windows incorrectly unless backing store is enabled.
# The source patches applied in vs/sdl2/ and src/gui/ (see README.kindle.md)
# work around all of these so DOSBox-X renders 1:1 and shows its window title.
#
# Usage:
#   KINDLE_TOOLCHAIN=/path/to/toolchain ./build-kindle.sh
#
# The default toolchain path can be overridden with the KINDLE_TOOLCHAIN
# environment variable. Example toolchain: crosstool-NG (NiLuJe) build for
# `arm-kindlepw2-linux-gnueabi` (GCC 14.x, glibc 2.12 sysroot).
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
TOOLCHAIN="${KINDLE_TOOLCHAIN:-/home/tqhyg/x-tools/arm-kindlepw2-linux-gnueabi}"
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

# --- Prerequisite checks -----------------------------------------------------
command -v "$HOST-gcc" >/dev/null 2>&1 || {
    echo "ERROR: cross compiler $HOST-gcc not found in $TC_BIN" >&2
    echo "       Set KINDLE_TOOLCHAIN to the toolchain prefix." >&2
    exit 1
}
for t in autoconf automake aclocal libtool make; do
    command -v "$t" >/dev/null 2>&1 || { echo "ERROR: $t not found (install autotools)" >&2; exit 1; }
done
[ -d "$SYSROOT" ] || { echo "ERROR: sysroot $SYSROOT not found" >&2; exit 1; }

mkdir -p "$ART"

# =============================================================================
# 1. Static SDL2 (X11 video, dummy audio only)
# =============================================================================
# Video:  only the X11 driver (dynamically dlopen's libX11.so.6 at runtime).
# Audio:  only disk + dummy drivers; the dummy driver is patched to be the
#         automatic fallback so no SDL_AUDIODRIVER env var is needed.
# Static: --enable-static --disable-shared so SDL2 is baked into dosbox-x.
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

# =============================================================================
# 2. Static zlib + libpng (built from the in-tree copies in vs/)
# =============================================================================
# The Kindle sysroot ships an inconsistent libpng (1.6 headers, 1.2 .so), so
# we build zlib 1.3.1 and libpng 1.6.x statically into a private prefix.
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

# =============================================================================
# 3. DOSBox-X
# =============================================================================
# SDL2 detection uses the custom static sdl2.pc via PKG_CONFIG_PATH.
#   --enable-sdl2         SDL2 build (not SDL1)
#   --disable-opengl      no GL on the Kindle X server
#   --disable-freetype    no TTF output (uses output=surface)
#   --disable-avcodec     avoid pulling in host FFmpeg
#   --disable-sdlnet/alsa-midi/libfluidsynth/libslirp   no such libs on device
#   --disable-dynamic-x86 --disable-dynrec              x86-only, not on ARM
# CPPFLAGS -D__STDC_FORMAT_MACROS fixes PRIx64 in whereami.c under C++.
# LDFLAGS -static-libgcc/-static-libstdc++ bake in the C++ runtime (the device
# has an old libstdc++ that lacks GLIBCXX_3.4.20+ symbols).
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

# Force static libatomic. `-latomic` is needed for 64-bit atomics on 32-bit
# ARM; GCC 14's libatomic.so may not exist on the old device, so link the .a.
sed -i 's/-latomic/-Wl,-Bstatic -latomic -Wl,-Bdynamic/g' Makefile src/Makefile

echo "==> Building DOSBox-X"
make -j"$(nproc)"

echo "==> Stripping and installing to build-artifacts"
"$STRIP" -o "$ART/dosbox-x" src/dosbox-x

# --- Report ------------------------------------------------------------------
echo "Done. Binary: $ART/dosbox-x"
file "$ART/dosbox-x" || true
echo "Dynamic dependencies (should only be glibc + X11, no SDL2/zlib/libpng/libstdc++):"
"$HOST-readelf" -d "$ART/dosbox-x" | grep NEEDED || true
