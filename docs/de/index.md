---
layout: default
title: inki
---

**Einmal einrichten und jahrelang laufen lassen.** inki ist das drahtlose ePaper-Display, das Live-Informationen genau dort anzeigt, wo du sie brauchst. Keine Kabel, keine Wartung — einfach Batterien einlegen und über deinen Browser konfigurieren.

**Ein Gerät, viele Anwendungen** — dieselbe Hardware macht alles möglich. Beginne mit Raumbuchung, wechsle zu Wetteranzeigen oder probiere Home-Automation-Überwachung. Verfügbar in kompakten 4,2" und großen 7,5" Größen mit 3D-druckbaren Gehäusen passend für deinen Raum.

Geeignet für Raumbuchung, Home-Automation-Überwachung, Wetteranzeigen und Datenvisualisierung.

<p align="center">
  <img src="{{ '/assets/images/esign_7_5_front.JPG' | relative_url }}" alt="inki 7,5 Zoll Display" width="300" style="border-radius: 8px; margin: 20px 0;">
</p>
**Hauptvorteile:**
- **Jahre Batterielaufzeit** — bis zu 10.000 Aktualisierungszyklen mit Standard-AA-Batterien
- **Browser-Setup** — Konfiguration über Wi-Fi-Hotspot, keine Programmierung erforderlich
- **ePaper-Display** — Inhalt bleibt auch bei ausgeschaltetem Gerät sichtbar
- **Drahtlose Updates** — Firmware- und Inhaltsupdates über Wi-Fi
- **Einfache Montage** — Schwalbenschwanz-Montagesystem für schnelle Installation und Entfernung

## Anwendungen

<div class="use-cases-grid">
  <a href="{{ '/de/seatsurfing' | relative_url }}" class="use-case-tile">
    <img src="{{ '/assets/images/esign_4_2_1_cropped.jpg' | relative_url }}" alt="Raumbuchungs-Display" class="tile-image">
    <h3>Seatsurfing</h3>
    <p>Raumbuchung und Desk-Sharing-Display</p>
    <small>REST-API-Integration für Live-Buchungsdaten</small>
  </a>

  <a href="{{ '/de/historian' | relative_url }}" class="use-case-tile">
    <img src="{{ '/assets/images/7_5_ccu-historian.JPG' | relative_url }}" alt="Zeitreihen-Visualisierung" class="tile-image">
    <h3>Historian</h3>
    <p>Zeitreihen-Datenvisualisierung</p>
    <small>CCU-Historian-Integration mit Trendanalyse</small>
  </a>

  <a href="{{ '/de/homematic' | relative_url }}" class="use-case-tile">
    <img src="{{ '/assets/images/inki-homematic_3.JPG' | relative_url }}" alt="Home-Automation-Display" class="tile-image">
    <h3>Homematic</h3>
    <p>Home-Automation-Display</p>
    <small>Live-Sensordaten und Gerätestatus-Überwachung</small>
  </a>

  <a href="{{ '/de/weathermap' | relative_url }}" class="use-case-tile">
    <h3>Weathermap</h3>
    <p>Echtzeit-Wetterkarten</p>
    <small>HTTPS WMS Kachel-Abruf mit TLS-Unterstützung</small>
  </a>

  <a href="https://github.com/c0de111/inki#build-your-own-inki" class="use-case-tile">
    <img src="{{ '/assets/images/assembly.gif' | relative_url }}" alt="inki Montage-Animation" class="tile-image">
    <h3>Eigenes inki bauen</h3>
    <p>Open-Source-Hardware und -Software</p>
    <small>Komplettpaket: Platine, Firmware und 3D-druckbares Gehäuse</small>
  </a>
</div>

## Technische Daten

| Spezifikation | Wert |
|---------------|------|
| **Versorgungsstrom** | ~80mA (aktiv), µA (Standby) |
| **Batterielaufzeit** | Bis zu 10.000 Aktualisierungszyklen |
| **Display** | 4,2" oder 7,5" ePaper (4 Graustufen) |
| **Konnektivität** | Wi-Fi 802.11b/g/n (2,4GHz), TLS 1.2/1.3 |
| **MCU** | Raspberry Pi Pico W (RP2040) |
| **RTC** | DS3231 mit Batterie-Backup |
| **Web-Interface** | Wi-Fi-Setup und OTA-Updates |
| **Stromversorgung** | 3x AA oder 3x AAA Batterien |

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
</style>

<p align="center">
<strong>Interesse an einem Kit?</strong><br>
<strong>Kontakt:</strong> <a href="mailto:c0de@posteo.de">c0de@posteo.de</a>
</p>
