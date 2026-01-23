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
# Default configuration
USB_BOOTLOADER_ENABLE=0
USE_CASE="SEATSURFING"  # Default use case

# ----------------------------------------------------------------------------- 
# Work around pico-sdk 2.1.0 + GCC 15 host build failure (pioasm lacks <cstdint>)
# This patches the SDK headers in place, idempotently, so fresh clones/build dirs work.
patch_pioasm_headers() {
  local sdk="$PICO_SDK_PATH"
  local files=(
    "$sdk/tools/pioasm/pio_types.h"
    "$sdk/tools/pioasm/output_format.h"
  )
  for f in "${files[@]}"; do
    [[ -f "$f" ]] || continue
    if ! grep -q '<cstdint>' "$f"; then
      echo "Patching $f to include <cstdint> (fixes GCC 15 build of pioasm)..."
      # Insert after the last existing std/SDK include to keep ordering stable
      perl -0777 -i -pe 's/(#include[^\n]*\n)(?=(\n|struct))/\1#include <cstdint>\n/ if ! /<cstdint>/' "$f"
    fi
  done
}

patch_pioasm_headers

# Parse command line arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --use-case|-u)
      USE_CASE="$2"
      shift 2
      ;;
    --historian)
      USE_CASE="HISTORIAN"
      shift
      ;;
    --homematic)
      USE_CASE="HOMEMATIC"
      shift
      ;;
    --seatsurfing)
      USE_CASE="SEATSURFING" 
      shift
      ;;
    --weathermap)
      USE_CASE="WEATHERMAP"
      shift
      ;;
    --new-usecase)
      USE_CASE="NEW_USECASE"
      shift
      ;;
    --bootloader-usb)
      USB_BOOTLOADER_ENABLE=1
      shift
      ;;
    --help|-h)
      echo "Usage: $0 [OPTIONS]"
      echo "Options:"
      echo "  --use-case|-u <CASE>   Set use case: SEATSURFING (default), HISTORIAN, HOMEMATIC, NEW_USECASE"
      echo "  --historian            Build for Historian use case"
      echo "  --homematic            Build for Homematic use case"
      echo "  --seatsurfing          Build for SeatSurfing use case (default)"
      echo "  --new-usecase          Build for new use case template"
      echo "  --help|-h              Show this help"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      echo "Use --help for usage information"
      exit 1
      ;;
  esac
done

# Validate use case
case $USE_CASE in
  SEATSURFING|HISTORIAN|HOMEMATIC|WEATHERMAP|NEW_USECASE)
    # Valid use case
    ;;
  *)
    echo "Error: Invalid use case '$USE_CASE'"
    echo "Valid options: SEATSURFING, HISTORIAN, HOMEMATIC, WEATHERMAP, NEW_USECASE"
    exit 1
    ;;
esac

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

# Run CMake with USB option and use case passed as variables
echo "Building for use case: $USE_CASE"
cmake -DUSB_BOOTLOADER_ENABLE=${USB_BOOTLOADER_ENABLE} \
      -DUSE_CASE_DEFINE="USE_CASE_${USE_CASE}" \
      "$SCRIPT_DIR"

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
