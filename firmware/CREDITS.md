# 📜 CREDITS & THIRD-PARTY LICENSE NOTICE

This project (eSign) incorporates open-source components developed and maintained by third parties. These components are included in accordance with their respective licenses. We gratefully acknowledge their contributions.

---

### 🧱 Raspberry Pi Pico SDK

- **Source**: [https://github.com/raspberrypi/pico-sdk](https://github.com/raspberrypi/pico-sdk)
- **License**: BSD 3-Clause License
- **Included in**: `third_party/pico-sdk`

The Raspberry Pi Pico SDK provides low-level access and abstractions for the RP2040 microcontroller. It is used for device initialization, GPIO, I2C, timers, and other MCU peripherals.

---

### 🌐 lwIP TCP/IP Stack

- **Source**: [https://savannah.nongnu.org/projects/lwip/](https://savannah.nongnu.org/projects/lwip/)
- **License**: Modified BSD License
- **Included in**: `third_party/lwip`

lwIP is a small, independent implementation of the TCP/IP protocol suite. It is integrated via the Pico SDK for Wi-Fi networking support.

---

### 📄 Waveshare Pico e-Paper Driver Code

- **Source**: [https://github.com/waveshareteam/Pico_ePaper_Code](https://github.com/waveshareteam/Pico_ePaper_Code)
- **License**: GNU General Public License v3.0 (GPL-3.0)
- **Included in**: `third_party/waveshare_epaper`

This repository provides C and Python drivers for Waveshare's Pico e-Paper displays. The code is used to interface with various e-Paper modules, facilitating display operations in the eSign project.

---

### ⏰ DS3231 RTC Driver for Raspberry Pi Pico

- **Source**: [https://github.com/alpertng02/pico-ds3231](https://github.com/alpertng02/pico-ds3231)
- **License**: MIT License
- **Included in**: `third_party/ds3231`

This library offers a driver for the DS3231 real-time clock module, tailored for the Raspberry Pi Pico. It supports time configuration, reading, alarm settings, and temperature data retrieval.

---

### 🔤 Ubuntu Font Family

- **Source**: [https://design.ubuntu.com/font/](https://design.ubuntu.com/font/)
- **License**: Ubuntu Font License 1.0
- **Included in**: `third_party/fonts/ubuntu`

The Ubuntu font is utilized for rendering clean and modern textual content on the ePaper display, enhancing the visual appeal of the eSign interface.

---

If you are the author or maintainer of any of the third-party components and wish to clarify or adjust attribution, please open an issue or contact the maintainers of this project.

---

**Main project licenses:**

- Firmware: [Apache License 2.0](../LICENSE)
- Hardware: [CERN Open Hardware License v2 – Strongly Reciprocal](../hardware/LICENSE)
