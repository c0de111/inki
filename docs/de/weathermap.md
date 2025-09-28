---
layout: default
title: Weathermap - inki
---

[← Zurück zur Startseite]({{ '/de/' | relative_url }})

**Echtzeit-Wetterkarten** - inki-weathermap

## Überblick

Der Weathermap-Anwendungsfall verbindet inki mit deutschen Behörden-Kartendiensten, um Echtzeit-Wetter-Overlays auf geografischen Basiskarten anzuzeigen, optimiert für ePaper-Displays.

<p align="center">
  <img src="{{ '/assets/images/weathermap_4_2.JPG' | relative_url }}" alt="Weathermap-Beispiel auf dem 4,2 Zoll Display" width="500" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Layout-Vorschau der Weathermap für das 4,2" Panel.
</p>

<p align="center">
  <img src="{{ '/assets/images/weathermap_7_5.JPG' | relative_url }}" alt="Weathermap-Beispiel auf dem 7,5 Zoll Display" width="500" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Layout-Vorschau der Weathermap für das 7,5" Panel.
</p>

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

{% include contact_cta_de.md %}
