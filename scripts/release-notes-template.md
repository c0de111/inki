## Overview

Full DIY package: enclosure, PCB, and firmware to build Tinta without a programmer.
Quick-start UF2s: prebuilt images to preview setup and the web interface on a bare Pico W; same firmware runs on fully assembled devices and supports OTA via the web UI.

## What's Included

- Hardware: 3D-printed enclosure files and custom PCB (RTC, power management, ePaper).
- Firmware: C code for RTC wakeup, Wi-Fi, web UI, HTTP client, and ePaper rendering; build/flash/UF2 scripts.
- Prebuilt UF2s: single-file images (bootloader + firmware + default config; valid_flag pre-set).

## Assets

- `inki_seatsurfing.uf2`: SeatSurfing room booking display.
- `inki_historian.uf2`: CCU-Historian time-series visualization.
- `inki_homematic.uf2`: Homematic home automation display.
- Source code archives (`zip`, `tar.gz`).

## Quick Start

1. Hold BOOTSEL while plugging in Pico W → `RPI-RP2` drive appears.
2. Copy one UF2 (`inki_seatsurfing`, `inki_historian`, or `inki_homematic`) → board reboots automatically.
3. Connect to AP `inki-setup` (password `12345678`) → open `http://192.168.4.1`.
4. Configure Wi-Fi, use-case settings, and optional logo via the browser.

## Hardware For Full Functionality

- RTC: DS3231 (timekeeping and power management).
- ePaper: Waveshare 7.5" V2 or 4.2" V2 (select panel in web UI).
- UF2s run on a bare Pico W to preview the web interface; ePaper rendering and time-based features enable when hardware is present.
