# esign
**eSign** is a compact, energy-efficient system for ePaper-based display of information gathered via Wi-Fi, as for instance room signage. It features RTC-based power control allowing for low power consumption, the project includes 3D-printed cases, and a custom PCB.


---

## Project Overview

This repository provides all design files, documentation, and code for the open hardware and firmware project *eSign*:

1. **3D-Printed Enclosure**
   Files and documentation for the modular eSign enclosure.

2. **Electronics**
   Custom PCB design with RTC, Pico W, and power management for ultra-low energy consumption.

3. **Firmware**
   C-based firmware for RTC wakeup, Wi-Fi sync, ePaper display handling, and modular room configuration.

---

## Features

- **ePaper Display Support**
  Compatible with Waveshare 7.5" V2 and 4.2" V2 displays.

- **Energy Efficiency**
  Hardware-controlled shutdown via RTC and MOSFET switching — no software sleep required, operated by standard AA or AAA batteries and optioonal coin cell.

- **Wi-Fi Connectivity**
  Periodically fetches content via HTTP.

- **Multi-Page Display**
  Up to 8 user-selectable display pages via pushbuttons.

- **Battery Voltage Monitoring**
  Monitoring of batteries via adc, hardware-controlled voltage divider activated only when needed for RTC supply.

- **For room signage application: Room Customization**
  Layouts and display content can be adjusted for room types (office, conference, seminar).
---

## 📁 Repository Structure

```
esign/
├── LICENSE                    # Top-level: explains dual-licensing
├── hardware/                 # KiCad project, schematics, BOM, PCB files
│   ├── LICENSE               # CERN-OHL-S v2.0
│   └── ...
├── firmware/                # C firmware source, headers, build system
│   ├── LICENSE               # Apache 2.0
│   └── ...
├── enclosure/               # 3D printable STL files and documentation
├── docs/                    # Markdown docs, tips, and background
├── images/                  # Photos, renderings, examples
└── README.md                # This file
```

---

## 📜 License

This project contains both hardware and software components, which are licensed under separate terms:

- All content in the **/hardware** directory is licensed under the **CERN Open Hardware License v2 - Strongly Reciprocal (CERN-OHL-S-2.0)**.
- All content in the **/firmware** directory is licensed under the **Apache License, Version 2.0**.

See the LICENSE files in the respective directories for full license texts and terms.

---

## 🚧 Status

🟢 **Active development** — repository being built step-by-step.

Want to follow or contribute? Star the repo, and stay tuned for updates!

