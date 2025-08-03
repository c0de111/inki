#!/bin/bash
# reset.sh – Soft reset for RP2040 using OpenOCD

# This command triggers a software reset via the Cortex-M0+ System Control Block (SCB)
# by writing to the AIRCR register and setting the SYSRESETREQ bit.
# This results in a clean reboot without cutting power.

# Configuration:
# - CMSIS-DAP debug interface (e.g. Raspberry Pi Debug Probe)
# - RP2040 target
# - SWD speed set to 5 MHz
# - AIRCR address: 0xE000ED0C
# - Reset value: 0x05FA0004 = VECTKEY (0x05FA) + SYSRESETREQ (bit 2)

openocd \
  -f interface/cmsis-dap.cfg \
  -f target/rp2040.cfg \
  -c "adapter speed 5000" \
  -c "init" \
  -c "mww 0xE000ED0C 0x05FA0004" \
  -c "shutdown"
