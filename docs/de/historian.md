---
layout: default
title: Historian - inki
---

[← Zurück zur Startseite](/de/)

**Zeitreihen-Datenvisualisierung** - inki-historian

<p align="center">
  <img src="{{ '/assets/images/7_5_ccu-historian.JPG' | relative_url }}" alt="Historian Temperatur-Visualisierung" width="500" style="border-radius: 8px; margin: 20px 0;">
</p>

## Überblick

Der Historian-Anwendungsfall verbindet inki mit CCU-Historian-Servern, um Zeitreihen-Sensordaten mit Trendanalyse und historischen Visualisierungen anzuzeigen.

## Funktionen

- **Zeitreihen-Sensordaten** Visualisierung
- **Temperaturdiagramme** mit Trendanalyse
- **Historische Datenpunkte** über konfigurierbare Zeiträume
- **Echtzeit-Datenüberwachung** mit periodischen Updates
- **Multi-Sensor-Unterstützung** für umfassende Überwachung

## Technische Details

- **Protokoll**: JSON-RPC-Verbindung zu CCU-Historian-Servern
- **Datenquellen**: Home-Automation-Sensoren und -Geräte
- **Visualisierung**: Diagramm-Rendering für ePaper-Display
- **Speicherverwaltung**: Effiziente Datenverarbeitung für begrenzten RAM
- **Update-Zyklen**: Konfigurierbare Aktualisierungsintervalle

## Datentypen

- Temperaturmessungen
- Feuchtigkeitswerte
- Energieverbrauchsdaten
- Gerätestatus-Informationen
- Historische Trendanalyse

## Konfiguration

Setup umfasst:
- CCU-Historian-Server-Verbindung
- Sensor-ID-Zuordnung
- Zeitbereich-Einstellungen
- Diagramm-Anzeigeeinstellungen
- Update-Häufigkeit

## Build

```bash
cd firmware/c
./build.sh --historian
```
