---
layout: default
title: Weathermap - inki
---

[← Back to homepage](/)

**Real-time weather mapping** - inki-weathermap

## Overview

The Weathermap use case connects inki to German government mapping services to display real-time weather map overlays on geographic base maps, optimized for ePaper displays.

## Features

- **Real-time weather maps** from official sources
- **WMS tile retrieval** from `sgx.geodatenzentrum.de`
- **HTTPS connectivity** with full TLS 1.2/1.3 support
- **Black-and-white processing** for ePaper compatibility
- **Memory-efficient streaming** for limited RAM constraints

## Technical Details

- **Protocol**: HTTPS with TLS 1.2/1.3 encryption
- **Data Source**: German Federal Agency for Cartography
- **Image Processing**: PNG to 1-bit conversion for ePaper
- **Security**: mbedTLS with certificate validation
- **Performance**: ~196KB/s transfer rate over TLS

## Implementation Status

### ✅ Phase 1 Complete: TLS Infrastructure
- TLS/HTTPS client with proper certificate validation
- Successful data transfer (334KB weather tiles)
- ALTCP integration with timeout optimization
- Complete cipher suite support

### 🔄 Phase 2 Planned: Image Processing
- Streaming PNG decompression
- RGB to 1-bit dithering
- Memory-constrained processing
- ePaper display integration

## Configuration

Setup includes:
- Map tile coordinates
- Zoom level preferences
- Update intervals
- Geographic region selection

## Build

```bash
cd firmware/c
./build.sh --weathermap
```

## Technical Achievements

- **TLS Handshake**: Complete in ~1.6 seconds
- **Data Transfer**: 334,808 bytes successfully received
- **Cipher Suite**: `TLS_AES_256_GCM_SHA384`
- **Memory Efficiency**: Count-only mode for large transfers