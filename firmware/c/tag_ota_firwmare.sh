#!/bin/sh

# Colors (optional, works in most terminals)
GREEN='\033[0;32m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Get Git version and date
GIT_VERSION=$(git describe --tags --always --dirty | cut -c1-32)
BUILD_DATE=$(date +%F)

# Archive folder
OUTDIR="firmware_archive"
mkdir -p "$OUTDIR"

# Source files
SRC0="./build/inki_slot0.bin"
SRC1="./build/inki_slot1.bin"

# Destination files
DEST0="${OUTDIR}/inki_slot0_${GIT_VERSION}_${BUILD_DATE}.bin"
DEST1="${OUTDIR}/inki_slot1_${GIT_VERSION}_${BUILD_DATE}.bin"

echo -e "${BOLD}Archiving Inki firmware builds${NC}"
echo " Git version : ${CYAN}${GIT_VERSION}${NC}"
echo " Build date  : ${CYAN}${BUILD_DATE}${NC}"
echo " Output dir  : ${CYAN}${OUTDIR}${NC}"
echo ""

# Copy and report
if [ -f "$SRC0" ]; then
    cp "$SRC0" "$DEST0"
    echo -e " ✅ Archived ${GREEN}$SRC0${NC} → ${CYAN}$DEST0${NC}"
else
    echo -e " ❌ ${SRC0} not found"
fi

if [ -f "$SRC1" ]; then
    cp "$SRC1" "$DEST1"
    echo -e " ✅ Archived ${GREEN}$SRC1${NC} → ${CYAN}$DEST1${NC}"
else
    echo -e " ❌ ${SRC1} not found"
fi

echo ""
echo -e "${BOLD}Done.${NC} Archived firmware is safe in ${CYAN}$OUTDIR${NC}."
#
