#!/bin/bash
set -e

RETROARCH_VERSION="${RETROARCH_VERSION:-v1.22.2}"
OUTPUT_DIR="${OUTPUT_DIR:-/output}"

echo "=== Building RetroArch ${RETROARCH_VERSION} for aarch64 ==="

# Clone RetroArch
if [ ! -d "RetroArch" ]; then
    git clone --depth 1 --branch "$RETROARCH_VERSION" \
        https://github.com/libretro/RetroArch.git
fi

cd RetroArch

# Apply common patches
if [ -d /patches/common ] && ls /patches/common/*.patch 1>/dev/null 2>&1; then
    for patch in /patches/common/*.patch; do
        echo "Applying: $(basename "$patch")"
        git apply "$patch"
    done
fi

# Stage the shared IGM sources on top of the patched tree
cp -r /common_src/* .
for f in spruce_igm_platform.h; do
    [ -f "$f" ] || { echo "FATAL: $f missing - common_src/ was not staged" >&2; exit 1; }
done

# Cross-compilation environment
export CC=aarch64-linux-gnu-gcc
export CXX=aarch64-linux-gnu-g++
export AR=aarch64-linux-gnu-ar
export STRIP=aarch64-linux-gnu-strip
export PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig
export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig

export CFLAGS="$CFLAGS -O3 -flto -ffunction-sections -fdata-sections -flto=auto -DHAVE_SCREEN_ORIENTATION -DGEOMETRY_MENU_ROTATION -D_GNU_SOURCE -DHAVE_FILTERS_BUILTIN"
export CXXFLAGS="$CXXFLAGS -O3 -ffunction-sections -fdata-sections -flto=auto -DHAVE_SCREEN_ORIENTATION -DGEOMETRY_MENU_ROTATION -D_GNU_SOURCE -DHAVE_FILTERS_BUILTIN"
export LDFLAGS="$LDFLAGS -Wl,--gc-sections -flto=auto"

# Configure for universal aarch64 binary
# Uses SDL2 + EGL + GLES + Vulkan. GLES works on all GPUs; Vulkan is available
# on devices with Vulkan drivers (e.g. Mali G57 on Smart Pro S).
CFLAGS="$CFLAGS" \
CXXFLAGS="$CXXFLAGS" \
LDFLAGS="$LDFLAGS" \
./configure --host=aarch64-linux-gnu \
    --disable-x11 \
    --disable-wayland \
    --enable-vulkan \
    --disable-opengl \
    --disable-qt \
    --disable-kms \
    --disable-pulse \
    --disable-jack \
    --disable-oss \
    --disable-discord \
    --enable-udev \
    --enable-opengles \
    --enable-opengles3 \
    --enable-egl \
    --enable-sdl2 \
    --enable-alsa \
    --enable-networking \
    --enable-ssl \
    --enable-command \
    --enable-freetype \
    --enable-builtinzlib \
    --enable-zlib

# Build
make HAVE_STATIC_VIDEO_FILTERS=1 HAVE_STATIC_AUDIO_FILTERS=1 -j$(nproc)

# Output
mkdir -p "$OUTPUT_DIR"
cp retroarch "$OUTPUT_DIR/"
aarch64-linux-gnu-strip -s "$OUTPUT_DIR/retroarch"

echo "=== Build complete: ${OUTPUT_DIR}/retroarch ==="
