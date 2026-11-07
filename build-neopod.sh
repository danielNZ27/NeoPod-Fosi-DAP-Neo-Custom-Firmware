#!/usr/bin/env bash
# macOS/Linux counterpart of build-neopod.ps1.
# Builds the Neo firmware image and SD-card companions into build/out
# (or the directory given with --out). Does not build the flasher apps.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION=182
OUT="$REPO/build/out"
CLEAN=0

usage() {
    echo "usage: $0 [--version N] [--out DIR] [--clean]" >&2
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) VERSION="${2:?}"; shift 2 ;;
        --out) OUT="${2:?}"; shift 2 ;;
        --clean) CLEAN=1; shift ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

# Prefer the official Arm GNU Toolchain in ../toolchains (it ships newlib
# headers; Homebrew's arm-none-eabi-gcc does not).
if [[ -x "$REPO/../toolchains/bin/arm-none-eabi-gcc" ]]; then
    export PATH="$(cd "$REPO/../toolchains/bin" && pwd):$PATH"
fi

for tool in cmake ninja arm-none-eabi-gcc arm-none-eabi-objcopy python3; do
    command -v "$tool" >/dev/null || { echo "error: $tool not found on PATH" >&2; exit 1; }
done

if ! echo '#include <string.h>' | arm-none-eabi-gcc -x c -fsyntax-only - 2>/dev/null; then
    echo "error: $(command -v arm-none-eabi-gcc) has no newlib headers." >&2
    echo "       Install the Arm GNU Toolchain into $REPO/../toolchains" >&2
    exit 1
fi

VENV="$REPO/.venv"
if [[ ! -x "$VENV/bin/python" ]]; then
    echo "Creating Python venv..."
    python3 -m venv "$VENV"
fi
if ! "$VENV/bin/python" -c 'import PIL' 2>/dev/null; then
    "$VENV/bin/pip" install -q pillow
fi
PY="$VENV/bin/python"

BUILD="$REPO/build/neo"
DEV="$BUILD/platform/device"
[[ $CLEAN -eq 1 ]] && rm -rf "$BUILD"
mkdir -p "$OUT/sd-card/.echo"

echo "Configuring PatchPod CMake..."
cmake -S "$REPO" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/arm-none-eabi.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE=-O2 -DECHO_BOARD=neo \
    -DECHO_BUILD_SIM=OFF -DECHO_BUILD_TESTS=OFF -DECHO_BUILD_DEVICE=ON -DECHO_RELEASE=ON \
    -DECHO_JACK_DIAGNOSTICS=OFF

echo "Building PatchPod targets..."
cmake --build "$BUILD" --target echo_device echo_rescan_overlay \
    echo_core1 echo_core1_mp3 echo_core1_aac echo_core1_opus

echo "Extracting companion binary decoders..."
arm-none-eabi-objcopy -O binary "$DEV/echo_core1_mp3.elf" "$DEV/core1_mp3.bin"
arm-none-eabi-objcopy -O binary "$DEV/echo_core1.elf" "$DEV/core1.bin"
arm-none-eabi-objcopy -O binary "$DEV/echo_core1_aac.elf" "$DEV/core1_aac.bin"
arm-none-eabi-objcopy -O binary "$DEV/echo_core1_opus.elf" "$DEV/core1_opus.bin"

IMG="$OUT/DP3_V${VERSION}.IMG"
echo "Packaging PatchPod firmware image $(basename "$IMG")..."
"$PY" "$REPO/tools/build_boot_image.py" "$REPO/assets/boot.png" "$OUT/sd-card/.echo/boot.rgb565"
"$PY" "$REPO/tools/build_neo_image.py" --version "$VERSION" \
    --elf "$DEV/echo_device.elf" --output "$IMG" \
    --overlay "$DEV/echo_rescan_overlay.elf" \
    --core1-mp3 "$DEV/core1_mp3.bin" \
    --core1-flac "$DEV/core1.bin" \
    --core1-aac "$DEV/core1_aac.bin" \
    --core1-opus "$DEV/core1_opus.bin" \
    --sd-output "$OUT/sd-card/.echo"

(cd "$OUT" && shasum -a 256 "$(basename "$IMG")" sd-card/.echo/* > SHA256SUMS.txt)

echo "Build complete!"
echo "Firmware image:     $IMG"
echo "Companion decoders: $OUT/sd-card/.echo/"
