---
layout: default
title: Weathermap - inki
---

[← Back to homepage]({{ '/' | relative_url }})

<div class="weathermap-wip-banner">
  **Real-time weather mapping** - inki-weathermap
</div>

## Overview

The Weathermap use case connects inki to German government mapping services to display real-time weather map overlays on geographic base maps, optimized for ePaper displays.

**Data Source**: The maps are provided by BKG (Bundesamt für Kartographie und Geodäsie - German Federal Agency for Cartography and Geodesy) through their free basemap.de WMS service. This official government service provides high-quality geographic base maps under the Creative Commons Attribution 4.0 license (CC BY 4.0), making it freely available for all users.

## Features (work in progress)

- **Real-time weather maps** from official sources
- **WMS tile retrieval** from `sgx.geodatenzentrum.de`
- **HTTPS connectivity** with full TLS 1.2/1.3 support
- **Memory-efficient streaming** for limited RAM constraints
- **Data Source**: German Federal Agency for Cartography
- **Image Processing**: PNG to 1-bit conversion for ePaper
- **Security**: mbedTLS with certificate validation

## Implementation Status

- TLS/HTTPS client with proper certificate validation
- Streaming PNG decompression
- fetching of geo data and plot on epaper in 4 gray scales or black and white


## Configuration

Setup includes:
- Map tile coordinates
- Zoom level preferences
- Update intervals
- Geographic region selection

<style>
.weathermap-wip-banner {
  position: relative;
  overflow: hidden;
  padding: 20px;
  margin: 20px 0;
  border: 2px solid #cc0000;
  border-radius: 8px;
  background: rgba(255, 0, 0, 0.05);
}

.weathermap-wip-banner::before {
  content: "WORK IN PROGRESS";
  position: absolute;
  top: 0;
  left: -50px;
  right: -50px;
  bottom: 0;
  background: rgba(255, 0, 0, 0.1);
  color: #cc0000;
  font-weight: bold;
  font-size: 16px;
  letter-spacing: 3px;
  transform: rotate(-35deg);
  display: flex;
  align-items: center;
  justify-content: center;
  pointer-events: none;
  z-index: 1;
}
</style>
