---
layout: default
title: Homematic - inki
---

[← Zurück zur Startseite]({{ '/de/' | relative_url }})

**Home-Automation-Display** - inki-homematic

<p align="center">
  <img src="{{ '/assets/images/inki-homematic_3.JPG' | relative_url }}" alt="Homematic Home-Automation-Display" width="400" style="border-radius: 8px; margin: 20px 0;">
</p>

## Überblick

Der Homematic-Anwendungsfall ermöglicht es inki, Live-Sensordaten und Gerätestatus von Homematic-Home-Automation-Systemen anzuzeigen und bietet Echtzeit-Überwachung deines Smart Homes.

## Funktionen

- **Live-Sensordaten** Überwachung
- **Gerätestatus** Informationen
- **Multi-Geräte-Unterstützung** für umfassende Home-Automation
- **Echtzeit-Updates** mit konfigurierbaren Intervallen
- **Kompakte Anzeige** optimiert für ePaper-Bildschirme

## Technische Details

- **Integration**: Direkte Verbindung zu Homematic-Systemen
- **Datenverarbeitung**: Effiziente Analyse von Gerätezuständen
- **Anzeigeformat**: Organisiertes Layout für mehrere Sensoren
- **Speicheroptimierung**: Optimiert für eingebettete Hardware
- **Fehlerbehandlung**: Robuste Kommunikationsprotokolle

## Unterstützte Datentypen

- Temperatur- und Feuchtigkeitssensoren
- Bewegungsmelder
- Tür-/Fensterkontakte
- Energiemessgeräte
- Schalterzustände
- System-Status-Informationen

## Konfiguration

Geräte-Setup umfasst:
- Homematic-System-Anmeldedaten
- Geräte-ID-Zuordnung
- Anzeige-Einstellungen
- Update-Intervalle
- Layout-Anpassung

## Build

```bash
cd firmware/c
./build.sh --homematic
```

{% include contact_cta_de.md %}
