---
layout: default
title: Weathermap - inki
---

[← Zurück zur Startseite]({{ '/de/' | relative_url }})

<div class="weathermap-wip-banner">
  **Echtzeit-Wetterkarten** - inki-weathermap
</div>

## Überblick

Der Weathermap-Anwendungsfall verbindet inki mit deutschen Behörden-Kartendiensten, um Echtzeit-Wetter-Overlays auf geografischen Basiskarten anzuzeigen und für ePaper-Displays zu optimieren.

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

**Datenquelle:** Die Karten stammen vom Bundesamt für Kartographie und Geodäsie (BKG) und werden über den kostenlosen basemap.de WMS-Dienst bereitgestellt. Der Dienst steht unter der Creative Commons Attribution 4.0-Lizenz (CC BY 4.0) und liefert hochwertige amtliche Geodaten.

## Funktionen (in Arbeit)

- **Echtzeit-Wetterkarten** aus offiziellen Quellen
- **WMS-Kachelabruf** von `sgx.geodatenzentrum.de`
- **HTTPS-Konnektivität** mit vollständiger TLS 1.2/1.3-Unterstützung
- **Speichereffizientes Streaming** für begrenzte RAM-Ressourcen
- **Datenquelle**: Bundesamt für Kartographie und Geodäsie (BKG)
- **Bildverarbeitung**: PNG-zu-1-Bit-Konvertierung für ePaper
- **Sicherheit**: mbedTLS mit Zertifikatsprüfung

## Implementierungsstatus

- TLS/HTTPS-Client mit vollständiger Zertifikatsprüfung
- Streaming-PNG-Dekomprimierung
- Geodaten abrufen und direkt auf dem ePaper in vier Graustufen oder Schwarz-Weiß darstellen

## Konfiguration

Setup umfasst:
- Karten-Kachel-Koordinaten
- Zoomlevel-Einstellungen
- Update-Intervalle
- Auswahl der geografischen Region

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
  content: "IN ARBEIT";
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

{% include contact_cta_de.md %}
