#!/bin/bash

CREDENTIALS_FILE="wifi_credentials.h"

if [ ! -f "$CREDENTIALS_FILE" ]; then
  echo "❌ Error: '$CREDENTIALS_FILE' not found."
  echo ""
  echo "Please create this file with your Wi-Fi credentials:"
  echo ""
  echo "#define WIFI_SSID     \"YourSSID\""
  echo "#define WIFI_PASSWORD \"YourPassword\""
  echo ""
  echo "⚠️  This file must NOT be committed to version control!"
  echo "You can start by copying the example:"
  echo "  cp wifi_credentials.h.example wifi_credentials.h"
  exit 1
fi

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

echo "Using: $CREDENTIALS_FILE for WIFI access"

# Ensure ROOM is set
if [ -z "$ROOM" ]; then
    echo "Error: ROOM is not set. Use: ROOM=LPB111H ./build.sh"
    exit 1
fi

echo "Building for room: $ROOM"

# Define the build directory relative to the script's location
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Create the build directory if it doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
fi

# Navigate to the build directory
cd "$BUILD_DIR" || exit 1

# Run cmake with the correct source and build paths
cmake -DROOM="$ROOM" "$SCRIPT_DIR"

# Force a rebuild
make -j4
