#!/bin/bash
set -e  # Exit immediately on error

# ----- Terminal colors -----
GREEN='\033[1;32m'
CYAN='\033[1;36m'
YELLOW='\033[1;33m'
RESET='\033[0m'

# ----- Start time -----
start_time=$(date +%s)
echo -e "${CYAN}Starting eSign flashing process...${RESET}"

# ----- Optional: Full chip erase, invoke by "--erase" -----
if [[ "$1" == "--erase" ]]; then
  echo -e "${YELLOW}Performing full chip erase...${RESET}"
  openocd --debug=0 -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
    -c "adapter speed 5000" \
    -c "init; reset halt; flash erase_address 0x10000000 0x100000; shutdown"
  echo -e "${GREEN}Chip erase complete.${RESET}"
  echo
fi

# ----- Helper: Set valid_flag = 1 at offset 13 -----
set_valid_flag() {
  local file="$1"
  local offset=13  # Offset of valid_flag in firmware_header_t
  printf "\x01" | dd of="$file" bs=1 seek=$offset count=1 conv=notrunc status=none
  echo -e "${GREEN}✓ Patched valid_flag = 1 in temporary file $file${RESET}"
}

# ----- Flash bootloader -----
echo -e "${YELLOW}Flashing bootloader to 0x10000000...${RESET}"
openocd --debug=0 -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "adapter speed 5000" \
  -c "program build/inki_bootloader.bin 0x10000000 reset exit"
sleep 1.5

# ----- Flash firmware slot 0 -----
# echo -e "${YELLOW}Flashing firmware (slot 0) to 0x10010000...${RESET}"
# echo -e "${CYAN}Creating temporary copy of inki_slot0.bin to avoid modifying the original...${RESET}"
# TMP_SLOT0=$(mktemp /tmp/inki_slot0_patched_XXXXXX.bin)
# cp build/inki_slot0.bin "$TMP_SLOT0"
# set_valid_flag "$TMP_SLOT0"
# openocd --debug=0 -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
#   -c "adapter speed 5000" \
#   -c "program $TMP_SLOT0 0x10010000 reset exit"
# rm -f "$TMP_SLOT0"
# echo -e "${GREEN}✓ Temporary file deleted after flashing.${RESET}"
# sleep 1.5

# # ----- Flash firmware slot 1 -----
echo -e "${YELLOW}Flashing firmware (slot 1) to 0x100FB800...${RESET}"
echo -e "${CYAN}Creating temporary copy of inki_slot1.bin to avoid modifying the original...${RESET}"
TMP_SLOT1=$(mktemp /tmp/inki_slot1_patched_XXXXXX.bin)
cp build/inki_slot1.bin "$TMP_SLOT1"
set_valid_flag "$TMP_SLOT1"
openocd --debug=0 -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "adapter speed 5000" \
  -c "program $TMP_SLOT1 0x100FB800 reset exit"
rm -f "$TMP_SLOT1"
echo -e "${GREEN}✓ Temporary file deleted after flashing.${RESET}"
# sleep 1.5

# # ----- Flash default config / factory reset (at 0x101E7000) -----
echo -e "${YELLOW}Flashing default config to 0x101E7000...${RESET}"
openocd --debug=0 -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "adapter speed 5000" \
  -c "program build/inki_default_config.bin 0x101E7000 reset exit"

# ----- Done -----
end_time=$(date +%s)
elapsed=$((end_time - start_time))

echo
echo -e "${GREEN}Flashing complete in ${elapsed} seconds.${RESET}"
