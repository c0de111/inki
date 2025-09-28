---
layout: default
title: Seatsurfing - inki
---

[← Zurück zur Startseite](/de/)

**Raumbuchung und Desk-Sharing-Display** - inki-seatsurfing

<p align="center">
  <img src="{{ '/assets/images/esign_4_2_1_cropped.jpg' | relative_url }}" alt="Seatsurfing Raumbuchungs-Display" width="400" style="border-radius: 8px; margin: 20px 0;">
</p>

## Überblick

Der Seatsurfing-Anwendungsfall verwandelt inki in ein intelligentes Raumbuchungs-Display, das Echtzeit-Verfügbarkeit und Terminplanung für Besprechungsräume und Arbeitsplätze anzeigt.

## Funktionen

- **Live-Buchungsdaten** über REST-API-Integration
- **Raumverfügbarkeitsstatus** mit Bewohnernamen
- **Terminplanung** für bevorstehende Meetings
- **Anpassbare Logos und Layouts** für Branding
- **Mehrere Räume pro Display** Unterstützung

## Technische Details

- **API-Integration**: Verbindung zu Seatsurfing-Servern über REST-API
- **Authentifizierung**: Basic-Authentifizierung mit konfigurierbaren Anmeldedaten
- **Datenformat**: JSON-Antwort-Parsing für Buchungsinformationen
- **Update-Häufigkeit**: Konfigurierbare Aktualisierungsintervalle
- **Anzeige**: Raumstatus, Bewohnernamen und Terminplanung

## Konfiguration

Die Gerätekonfiguration umfasst:
- Seatsurfing-Server-Anmeldedaten
- Orts-IDs für Raumzuordnung
- Aktualisierungsintervalle
- Anzeige-Layout-Einstellungen

## Build

```bash
cd firmware/c
./build.sh --seatsurfing
```
