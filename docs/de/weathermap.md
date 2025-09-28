---
layout: default
title: Weathermap - inki
---

[← Zurück zur Startseite](/de/)

**Echtzeit-Wetterkarten** - inki-weathermap

## Überblick

Der Weathermap-Anwendungsfall verbindet inki mit deutschen Behörden-Kartendiensten, um Echtzeit-Wetter-Overlays auf geografischen Basiskarten anzuzeigen, optimiert für ePaper-Displays.

## Funktionen

- **Echtzeit-Wetterkarten** von offiziellen Quellen
- **WMS-Kachel-Abruf** von `sgx.geodatenzentrum.de`
- **HTTPS-Konnektivität** mit vollständiger TLS 1.2/1.3-Unterstützung
- **Schwarz-Weiß-Verarbeitung** für ePaper-Kompatibilität
- **Speichereffizientes Streaming** für begrenzte RAM-Beschränkungen

## Technische Details

- **Protokoll**: HTTPS mit TLS 1.2/1.3-Verschlüsselung
- **Datenquelle**: Bundesamt für Kartographie und Geodäsie
- **Bildverarbeitung**: PNG zu 1-Bit-Konvertierung für ePaper
- **Sicherheit**: mbedTLS mit Zertifikatvalidierung
- **Performance**: ~196KB/s Übertragungsrate über TLS

## Implementierungsstatus

### ✅ Phase 1 Abgeschlossen: TLS-Infrastruktur
- TLS/HTTPS-Client mit ordnungsgemäßer Zertifikatvalidierung
- Erfolgreiche Datenübertragung (334KB Wetter-Kacheln)
- ALTCP-Integration mit Timeout-Optimierung
- Vollständige Cipher-Suite-Unterstützung

### 🔄 Phase 2 Geplant: Bildverarbeitung
- Streaming-PNG-Dekomprimierung
- RGB zu 1-Bit-Dithering
- Speicherbeschränkte Verarbeitung
- ePaper-Display-Integration

## Konfiguration

Setup umfasst:
- Karten-Kachel-Koordinaten
- Zoom-Level-Einstellungen
- Update-Intervalle
- Geografische Regionsauswahl

## Build

```bash
cd firmware/c
./build.sh --weathermap
```

## Technische Errungenschaften

- **TLS-Handshake**: Abgeschlossen in ~1,6 Sekunden
- **Datenübertragung**: 334.808 Bytes erfolgreich empfangen
- **Cipher-Suite**: `TLS_AES_256_GCM_SHA384`
- **Speichereffizienz**: Count-only-Modus für große Übertragungen