---
layout: default
title: Historian - inki
---

[← Zurück zur Startseite]({{ '/de/' | relative_url }})

**Zeitreihen-Datenvisualisierung** - inki-historian

<p align="center">
  <img src="{{ '/assets/images/7_5_ccu-historian.JPG' | relative_url }}" alt="Historian Temperatur-Visualisierung" width="500" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Temperaturverlauf eines Homematic-Sensors in unserem Garten über die vergangenen 24 Stunden
</p>

## Überblick
In der Hausautomation fallen viele ständig wechselnde und interessante Messwerte an. Natürlich lassen sie sich jederzeit über Weboberflächen oder Apps abrufen, aber es ist besonders komfortabel, wenn die wichtigsten Informationen automatisch auf einem dedizierten „elektronischen Bild“ erscheinen, das im Alltag immer im Blickfeld ist. Oben ist die 7,5"-Variante von inki zu sehen, die den Temperaturverlauf aus dem CCU-Historian der vergangenen 24 Stunden sowie den Gasverbrauch unseres Hauses zeigt.

<p align="center">
  <img src="{{ '/assets/images/inki_historian_gas.JPG' | relative_url }}" alt="Historian Gas-Visualisierung" width="500" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Gasverbrauch unserer Heizungsanlage – wie erhofft fällt im Sommer nur ein sehr geringer Verbrauch an
</p>

Der Historian-Anwendungsfall verbindet inki direkt mit [CCU-Historian](https://github.com/mdzio/ccu-historian)-Servern und stellt dir Zeitreihendaten mit Trendanalyse und historischen Visualisierungen drahtlos auf dem ePaper-Display bereit. Prinzipiell lassen sich alle Datenpunkte aus dem CCU-Historian abrufen und anzeigen – einige Beispiele findest du unten.

## Beispiele und Funktionen

- **Temperaturüberwachung** mit grafischer Trenddarstellung
- **Gasverbrauch der Heizung** mit täglicher Verlaufskurve
- **Automatischer Datenabruf** über die JSON-RPC-Schnittstelle des CCU-Historian
- **Extrem niedriger Energiebedarf** mit rund 10.000 Abfragen pro Batteriesatz
- **Drahtlose Konfiguration** über die Weboberfläche inklusive WLAN-Setup

## Konfiguration & Setup

Sobald dein inki eingeschaltet und verbunden ist, kannst du die Historian-Einstellungen bequem über die Weboberfläche konfigurieren. So sieht die Oberfläche aus:

<p align="center">
  <img src="{{ '/assets/images/inki-historian-webinterface.png' | relative_url }}" alt="Historian Weboberfläche Hauptkonfiguration" width="300" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Hauptkonfigurationsseite mit Servereinstellungen und Sensor-Auswahl für den CCU-Historian
</p>

**Webinterface-Einstellungen:**
- CCU-Historian-Serveradresse und Port definieren
- Sensor-IDs für bis zu 6 Datenpunkte auswählen und zuordnen
- Abfrageintervalle planen (optimiert für lange Batterielaufzeit)
- Darstellungsoptionen und Skalierungen für die Diagramme festlegen
- WLAN-Zugangsdaten und Netzwerkeinstellungen hinterlegen

<p align="center">
  <img src="{{ '/assets/images/inki-historian-webinterface_2.png' | relative_url }}" alt="Historian Weboberfläche Sensorkonfiguration" width="300" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Detailansicht der Sensor-Konfiguration mit Datenpunkt-IDs und Beschriftungen für Temperaturmessungen
</p>

**Setup-Schritte:**
1. Mit dem inki-Setup-WLAN verbinden
2. Die Weboberfläche unter http://192.168.4.1 aufrufen
3. Eigenes WLAN konfigurieren
4. CCU-Historian-Serverdaten eintragen
5. Sensor-IDs aus dem Hausautomationssystem auswählen
6. Abfrageintervalle und Anzeigeoptionen festlegen und speichern

**Firmware-Updates:**
- OTA-Updates direkt über die Weboberfläche durchführen
- Konfiguration sichern und wiederherstellen
- Batteriestatus überwachen und Warnungen anzeigen

**Entwicklungsstand:**
- Kernfunktionen sind getestet und einsatzbereit
- Fehlerbehandlung und Sonderfälle werden kontinuierlich erweitert
- Aktive Weiterentwicklung mit Feedback aus der Community

## Technische Umsetzung

- **Protokoll**: JSON-RPC-Integration mit CCU-Historian-Servern
- **Energiemanagement**: Vollständiges Abschalten zwischen geplanten Updates
- **Weck-/Schlafzyklus**: RTC-gesteuerte Aktivierung in konfigurierbaren Intervallen
- **Datenverarbeitung**: Grafikaufbereitung speziell für ePaper-Displays
- **Batterielaufzeit**: Mehrjährige Laufzeit abhängig vom Update-Intervall
- **Netzwerk**: Automatische WLAN-Verbindung und Datensynchronisation

{% include contact_cta_de.md %}
