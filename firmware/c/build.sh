#!/bin/bash

# -----------------------------------------------------------------------------
# build.sh – Full build pipeline for inki firmware (Pico W)
#
# This script compiles the firmware, trims unused flash sections,
# generates a firmware metadata header (CRC32, size, version, timestamp),
# and creates OTA-compatible and SWD-flashable images.
#
# Key features:
# - Uses CMake and make to build from source
# - Trims the final binary to exclude .config_flash (for OTA or minimal flashing)
# - Extracts Git version and build timestamp from generated version.c
# - Compiles and uses gen_firmware_header to prepend metadata to OTA binaries
# - Produces:
#     - esign.bin            → full firmware binary
#     - esign_trimmed.bin    → firmware only (no config)
#     - esign_trimmed.hex    → SWD-compatible hex format
#     - esign_header.bin     → 74-byte metadata header
#     - esign_OTA.bin        → header + trimmed binary (for OTA updates)
#
# -----------------------------------------------------------------------------
export PICO_SDK_PATH="${PICO_SDK_PATH:-$HOME/pico/pico-sdk}"

# Enable or disable USB <-> Serial support for debugging in the bootloader (1 = enabled, 0 = disabled)
USB_BOOTLOADER_ENABLE=0

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

VERSION_HEADER="version.h"
TEMPLATE_HEADER="version_template.h.in"

if [ ! -f "$VERSION_HEADER" ]; then
  echo "⚠️  Warning: '$VERSION_HEADER' not found."
  echo "Creating it from template: '$TEMPLATE_HEADER'"
  echo ""

  if [ -f "$TEMPLATE_HEADER" ]; then
    cp "$TEMPLATE_HEADER" "$VERSION_HEADER"
    echo "✅ '$VERSION_HEADER' created."
  else
    echo "❌ Error: '$TEMPLATE_HEADER' not found. Cannot proceed."
    exit 1
  fi
fi


# Print USB bootloader status in color
if [ "$USB_BOOTLOADER_ENABLE" -eq 1 ]; then
    echo -e "\033[1;31m[INFO] USB-UART Bootloader ENABLED -> careful, only for debugging! Jumping to the applications might not work.\033[0m"  # red
else
    echo -e "\033[1;32m[INFO] USB-UART Bootloader DISABLED\033[0m" # green
fi

# Define the build directory relative to the script's location
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Create the build directory if it doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
fi

# Navigate to the build directory
cd "$BUILD_DIR" || exit 1

# Run CMake with USB option passed as a variable
cmake -DUSB_BOOTLOADER_ENABLE=${USB_BOOTLOADER_ENABLE} "$SCRIPT_DIR"

# Force a rebuild
make -j4

# Generate binary files from ELF files
arm-none-eabi-objcopy -O binary inki_bootloader.elf inki_bootloader.bin
arm-none-eabi-objcopy -O binary inki_slot0.elf inki_slot0.bin
arm-none-eabi-objcopy -O binary inki_slot1.elf inki_slot1.bin
arm-none-eabi-objcopy -O binary inki_default_config.elf inki_default_config.bin

# patch binaries
BUILD_DIR="$SCRIPT_DIR/build"
CRC32SUM="$SCRIPT_DIR/crc32sum"
BIN_SLOT0="$BUILD_DIR/inki_slot0.bin"
BIN_SLOT1="$BUILD_DIR/inki_slot1.bin"

patch_firmware() {
    local BIN="$1"
    local SLOT="$2"

    echo "→ Patching $BIN (slot $SLOT)"

    if [ ! -f "$BIN" ]; then
        echo "ERROR: File not found: $BIN"
        exit 1
    fi

    SIZE=$(stat -c %s "$BIN")
    CRC=$("$CRC32SUM" "$BIN")

    if [ -z "$CRC" ]; then
        echo "❌ ERROR: CRC32 calculation failed – check path to crc32sum!"
        exit 1
    fi

    printf "   Size = %d bytes\n" "$SIZE"
    printf "   CRC32 = 0x%08X\n" "$CRC"

    # Patch firmware_size at offset 62 (4 bytes)
    printf "%08x" "$SIZE" | tac -rs .. | xxd -r -p | \
        dd of="$BIN" bs=1 seek=62 count=4 conv=notrunc status=none

    # Patch crc32 at offset 67 (4 bytes)
    printf "%08x" "$CRC" | tac -rs .. | xxd -r -p | \
        dd of="$BIN" bs=1 seek=67 count=4 conv=notrunc status=none
}

patch_firmware "$BIN_SLOT0" 0
patch_firmware "$BIN_SLOT1" 1

echo "✅ Firmware headers patched successfully."
