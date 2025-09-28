---
layout: default
title: inki
---

**Set it up once, and it runs for years.** inki is the wireless epaper display that shows live information exactly where you need it. No cables, no maintenance — just insert batteries and configure via your browser.

**One device, multiple applications** — the same hardware runs them all. Available in compact 4.2" and large 7.5" sizes with 3D printable enclosures to fit your space.

Suitable for room booking, home automation monitoring, weather displays, and data visualization.

<p align="center">
  <img src="{{ '/assets/images/esign_7_5_front.JPG' | relative_url }}" alt="inki 7.5 inch display" width="300" style="border-radius: 8px; margin: 20px 0;">
</p>
**Key Benefits:**
- **Years of battery life** — up to 10,000 refresh cycles on standard AA batteries
- **Browser setup** — configure via Wi-Fi hotspot, no programming required
- **epaper display** — content stays visible even when powered off
- **Wireless updates** — firmware and content updates over Wi-Fi
- **Easy mounting** — dovetail mount for quick installation and removal

## Applications

<div class="use-cases-grid">
  <a href="{{ '/seatsurfing' | relative_url }}" class="use-case-tile">
    <img src="{{ '/assets/images/esign_4_2_1_cropped.jpg' | relative_url }}" alt="Room booking display" class="tile-image">
    <h3>Seatsurfing</h3>
    <p>Room booking and desk sharing display</p>
    <small>REST API integration for live booking data</small>
  </a>

  <a href="{{ '/historian' | relative_url }}" class="use-case-tile">
    <img src="{{ '/assets/images/7_5_ccu-historian.JPG' | relative_url }}" alt="Time-series visualization" class="tile-image">
    <h3>Historian</h3>
    <p>Time-series data visualization</p>
    <small>CCU-Historian integration with trend analysis</small>
  </a>

  <a href="{{ '/homematic' | relative_url }}" class="use-case-tile">
    <img src="{{ '/assets/images/inki-homematic_3.JPG' | relative_url }}" alt="Home automation display" class="tile-image">
    <h3>Homematic</h3>
    <p>Home automation display</p>
    <small>Live sensor data and device status monitoring</small>
  </a>

  <a href="{{ '/weathermap' | relative_url }}" class="use-case-tile weathermap-wip">
    <h3>Weathermap</h3>
    <p>Real-time weather mapping</p>
    <small>Free basemap.de WMS service from BKG (CC BY 4.0)</small>
  </a>

  <a href="https://github.com/c0de111/inki#build-your-own-inki" class="use-case-tile">
    <img src="{{ '/assets/images/assembly.gif' | relative_url }}" alt="inki assembly animation" class="tile-image">
    <h3>Build your own inki</h3>
    <p>Open source hardware and software</p>
    <small>Complete package: PCB, firmware, and 3D printable enclosure</small>
  </a>
</div>

## Engineering Facts

| Specification | Value |
|---------------|-------|
| **Supply Current** | ~80mA (active), µA (sleep) |
| **Battery Life** | Up to 10,000 refresh cycles |
| **Display** | 4.2" or 7.5" ePaper (4 gray scales) |
| **Connectivity** | Wi-Fi 802.11b/g/n (2.4GHz), TLS 1.2/1.3 |
| **MCU** | Raspberry Pi Pico W (RP2040) |
| **RTC** | DS3231 with battery backup |
| **Web Interface** | Wi-Fi setup and OTA updates |
| **Power** | 3x AA or 3x AAA batteries |

## Quick Setup (for kit builders)

**Got your inki hardware ready?** Here's how to get started:

1. **Download firmware**
   - Get the latest UF2 from [GitHub Releases](https://github.com/c0de111/inki/releases/latest)
   - Choose: `inki_historian.uf2`, `inki_seatsurfing.uf2`, or `inki_homematic.uf2`

2. **Flash to Pico W**
   - Hold BOOTSEL button while connecting USB
   - Copy UF2 file to RPI-RP2 drive
   - Device restarts automatically

3. **Configure via browser**
   - Connect to WiFi "inki-setup" (password: `12345678`)
   - Open http://192.168.4.1 in browser
   - Set your WiFi, ePaper model, and application settings

**Note:** Works as web preview with just Pico W. Add DS3231 + ePaper for full automation.

<style>
.use-cases-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
  gap: 20px;
  margin: 20px 0;
}

.use-case-tile {
  border: 1px solid #ddd;
  border-radius: 8px;
  padding: 20px;
  text-align: center;
  background: #f9f9f9;
  transition: transform 0.2s, box-shadow 0.2s;
  text-decoration: none;
  color: inherit;
  display: block;
}

.use-case-tile:hover {
  transform: translateY(-2px);
  box-shadow: 0 4px 8px rgba(0,0,0,0.1);
}

.use-case-tile h3 {
  margin: 0 0 10px 0;
  font-size: 1.2em;
}

.use-case-tile p {
  margin: 10px 0;
  font-weight: bold;
}

.use-case-tile small {
  color: #666;
  font-style: italic;
}

.tile-image {
  width: 80px;
  height: auto;
  border-radius: 4px;
  margin-bottom: 10px;
  float: right;
  opacity: 0.7;
}

.weathermap-wip {
  position: relative;
  overflow: hidden;
}

.weathermap-wip::before {
  content: "WORK IN PROGRESS";
  position: absolute;
  top: 0;
  left: -50px;
  right: -50px;
  bottom: 0;
  background: rgba(255, 0, 0, 0.1);
  color: #cc0000;
  font-weight: bold;
  font-size: 14px;
  letter-spacing: 2px;
  transform: rotate(-35deg);
  display: flex;
  align-items: center;
  justify-content: center;
  pointer-events: none;
  z-index: 1;
}
</style>

<p align="center">
<strong>Interested in a kit?</strong><br>
<strong>Contact:</strong> <a href="mailto:c0de@posteo.de">c0de@posteo.de</a>
</p>
