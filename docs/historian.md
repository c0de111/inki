---
layout: default
title: Historian - inki
---

[← Back to homepage](/)

**Time-series data visualization** - inki-historian

<p align="center">
  <img src="/assets/images/7_5_ccu-historian.JPG" alt="Historian temperature visualization" width="500" style="border-radius: 8px; margin: 20px 0;">
</p>

## Overview

The Historian use case connects inki to CCU-Historian servers to display time-series sensor data with trend analysis and historical visualizations.

## Features

- **Time-series sensor data** visualization
- **Temperature graphs** with trend analysis
- **Historical data points** over configurable time ranges
- **Real-time data monitoring** with periodic updates
- **Multi-sensor support** for comprehensive monitoring

## Technical Details

- **Protocol**: JSON-RPC connection to CCU-Historian servers
- **Data Sources**: Home automation sensors and devices
- **Visualization**: Graph rendering for ePaper display
- **Memory Management**: Efficient data processing for limited RAM
- **Update Cycles**: Configurable refresh intervals

## Data Types

- Temperature measurements
- Humidity readings
- Energy consumption data
- Device status information
- Historical trend analysis

## Configuration

Setup includes:
- CCU-Historian server connection
- Sensor ID mapping
- Time range preferences
- Graph display settings
- Update frequency

## Build

```bash
cd firmware/c
./build.sh --historian
```