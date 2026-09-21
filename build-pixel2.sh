#!/bin/bash
set -e

RETROARCH_REF="${RETROARCH_REF:-e5eff6db27cd37c3c318741ee8bb9a3b8b60ec62}"

echo "=== Building RetroArch for Pixel2 (aarch64 native) ==="
echo "=== Ref: ${RETROARCH_REF} ==="

# Set up ccache
# export CCACHE_DIR="${CCACHE_DIR:-/ccache}"
# export CC="ccache gcc"
# export CXX="ccache g++"
# ccache --max-size=500M
# ccache --zero-stats

# Clone RetroArch and checkout pinned commit
if [ ! -d "RetroArch" ]; then
    git clone https://github.com/libretro/RetroArch.git
    cd RetroArch
    git checkout "$RETROARCH_REF"
else
    cd RetroArch
fi

# Fix CRLF line endings (upstream has mixed CRLF/LF that break git apply)
find . -type f \( -name '*.c' -o -name '*.h' \) -exec sed -i 's/\r$//' {} +

# Apply pixel2-specific patches only
if [ -d ../patches/pixel2 ] && ls ../patches/pixel2/*.patch 1>/dev/null 2>&1; then
    for patch in ../patches/pixel2/*.patch; do
        echo "Applying: $(basename "$patch")"
        git apply "$patch"
    done
fi

# Stage the shared IGM sources on top of the patched tree
cp -r ../common_src/* .
for f in spruce_igm_platform.h; do
    [ -f "$f" ] || { echo "FATAL: $f missing - common_src/ was not staged" >&2; exit 1; }
done

# Configure — Hario's exact flags for Pixel2 (RK3566 / Mali-G52)

export CFLAGS="-Ofast -march=armv8-a -mtune=cortex-a35 -ffunction-sections -fdata-sections -fomit-frame-pointer -flto=auto -DNDEBUG -DHAVE_FILTERS_BUILTIN -DIGM_VARIANT_PIXEL2"
export CXXFLAGS="$CFLAGS"
export LDFLAGS="$LDFLAGS -Wl,--gc-sections -flto=auto"

CFLAGS="$CFLAGS" \
CXXFLAGS="$CXXFLAGS" \
LDFLAGS="$LDFLAGS" \
./configure --disable-qt \
            --disable-discord \
            --disable-neon \
            --disable-vg \
            --disable-sdl \
            --disable-x11 \
            --disable-vulkan \
            --disable-vulkan_display \
            --disable-opengl1 \
            --disable-opengl_core \
            --disable-jack \
            --enable-alsa \
            --enable-udev \
            --enable-zlib \
            --enable-freetype \
            --enable-sdl2 \
            --enable-kms \
            --enable-ffmpeg \
            --enable-wayland \
            --enable-opengles \
            --enable-opengles3 \
            --enable-opengles3_1 \
            --enable-opengles3_2 \
            --enable-opengl

# Build
make HAVE_STATIC_VIDEO_FILTERS=1 HAVE_STATIC_AUDIO_FILTERS=1 -j$(nproc)
strip -s retroarch

# echo "=== ccache stats ==="
# ccache --show-stats

echo "=== Build complete: ${OUTPUT_DIR}/retroarch ==="
