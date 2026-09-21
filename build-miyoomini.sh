#!/bin/bash
set -e

RETROARCH_VERSION="${RETROARCH_VERSION:-v1.22.2}"
OUTPUT_DIR="${OUTPUT_DIR:-/output}"

echo "=== Building RetroArch ${RETROARCH_VERSION} for Miyoo Mini (armhf) ==="

# Clone RetroArch
if [ ! -d "RetroArch" ]; then
    git clone --depth 1 --branch "$RETROARCH_VERSION" \
        https://github.com/libretro/RetroArch.git
fi

cd RetroArch

# Fix line endings before patching (upstream has mixed CRLF/LF)
find . -type f \( -name '*.c' -o -name '*.h' \) -exec sed -i 's/\r$//' {} +

# Apply miyoomini-specific patches
# OnionUI patches (000xx) use patch(1) for fuzz tolerance
# Spruce patches (001xx) use git apply for strict format
for p in /patches/miyoomini/*.patch; do
    echo "Applying: $(basename "$p")"
    case "$(basename "$p")" in
        000*) patch -p1 < "$p" ;;
        *)    git apply "$p" ;;
    esac
done

# Stage the shared IGM sources on top of the patched tree
cp -r /common_src/* .
for f in spruce_igm_platform.h spruce_igm_theme.h spruce_igm_theme.c; do
    [ -f "$f" ] || { echo "FATAL: $f missing - common_src/ was not staged" >&2; exit 1; }
done

# Copy miyoomini custom source files on top of the patched tree
cp -r /miyoomini_src/* .

# Build using the miyoomini Makefile
make -f Makefile.miyoomini -j$(nproc)

# Output
mkdir -p "$OUTPUT_DIR"
cp retroarch "$OUTPUT_DIR/"
/opt/miyoomini-toolchain/usr/bin/arm-linux-gnueabihf-strip -s "$OUTPUT_DIR/retroarch"

echo "=== Build complete: ${OUTPUT_DIR}/retroarch ==="
