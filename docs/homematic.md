---
layout: default
title: Homematic - inki
---

[← Back to homepage](/)

**Home automation display** - inki-homematic

<p align="center">
  <img src="/assets/images/inki-homematic_3.JPG" alt="Homematic home automation display" width="400" style="border-radius: 8px; margin: 20px 0;">
</p>

## Overview

The Homematic use case enables inki to display live sensor data and device status from Homematic home automation systems, providing real-time monitoring of your smart home.

## Features

- **Live sensor data** monitoring
- **Device status** information
- **Multi-device support** for comprehensive home automation
- **Real-time updates** with configurable intervals
- **Compact display** optimized for ePaper screens

## Technical Details

- **Integration**: Direct connection to Homematic systems
- **Data Processing**: Efficient parsing of device states
- **Display Format**: Organized layout for multiple sensors
- **Memory Optimization**: Streamlined for embedded hardware
- **Error Handling**: Robust communication protocols

## Supported Data Types

- Temperature and humidity sensors
- Motion detectors
- Door/window contacts
- Energy meters
- Switch states
- System status information

## Configuration

Device setup includes:
- Homematic system credentials
- Device ID mapping
- Display preferences
- Update intervals
- Layout customization

## Build

```bash
cd firmware/c
./build.sh --homematic
```