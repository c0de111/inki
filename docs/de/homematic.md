---
layout: default
title: Homematic - inki
---

[← Zurück zur Startseite]({{ '/de/' | relative_url }})

**Home-Automation-Display** - inki-homematic

<p align="center">
  <img src="{{ '/assets/images/inki-homematic_3.JPG' | relative_url }}" alt="Homematic Home-Automation-Display" width="500" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Homematic-Sensordaten auf dem 4,2" ePaper: Temperatur, Heizungsstatus und Servicemeldungen im Überblick
</p>

## Überblick
Hausautomationssysteme sammeln kontinuierlich Sensordaten und Gerätestatusmeldungen aus dem gesamten Zuhause. Zwar lassen sich diese Werte jederzeit über Weboberflächen oder Apps abrufen, doch ein dediziertes „elektronisches Dashboard“ macht den Blick im Alltag viel komfortabler. Auf dem Foto oben zeigt inki Live-Daten aus unserem Homematic-System – Temperaturen von HmIP-STE2-PCB-Sensoren, Heizungssteuerung vom HmIP-WTH-1 sowie Leistungsaufnahme über HmIP-PSM-2.

Inki-homematic verbindet sich direkt per XML-RPC mit deiner Homematic-CCU, ruft Sensordaten und Gerätestatuswerte ab und stellt sie auf dem ePaper-Display dar. Die Integration fügt sich nahtlos in bestehende Homematic-Installationen ein. Praktisch jeder Geräteparameter kann über die Weboberfläche ausgewählt und angezeigt werden – einige Beispiele findest du unten.

<p align="center">
  <iframe src="https://makertube.net/videos/embed/bWefBKAkkHVg7govr5T3QV" allowfullscreen="" sandbox="allow-same-origin allow-scripts allow-popups" width="560" height="315" frameborder="0" style="border-radius: 8px; margin: 15px 0;"></iframe>
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Live-Demonstration: inki-homematic zeigt Echtzeit-Sensordaten und aktualisiert sie automatisch
</p>

## Beispiele

- **Temperaturüberwachung** mit HmIP-STE2-PCB-Sensoren in Echtzeit
- **Heizungssteuerung** mit Ziel- und Ist-Temperaturen des HmIP-WTH-1
- **Energie- und Schaltzustände** über HmIP-PSM-2 inklusive Leistungsanzeige
- **XML-RPC-Integration** für direkte CCU-Kommunikation ohne Zusatzsoftware
- **Servicemeldungen** der CCU werden automatisch eingeblendet
- **Extrem niedriger Energiebedarf** mit rund 10.000 Abfragen pro Batteriesatz
- **Komfortable Webkonfiguration** inklusive automatischer Firmware-Updates

## Konfiguration & Setup

Die Einrichtung erfolgt bequem über die Weboberfläche – zum Beispiel direkt vom Smartphone aus. So sehen die Konfigurationsseiten aus:

<p align="center">
  <img src="{{ '/assets/images/inki-homematic_webinterface2.png' | relative_url }}" alt="Homematic Weboberfläche Geräte-Konfiguration" width="300" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Hauptkonfigurationsseite für die Homematic-Anbindung
</p>

Die spezifischen Homematic-Einstellungen erreichst du über die jeweiligen Unterseiten und kannst dort die relevanten Parameter setzen:

**Webinterface-Einstellungen:**
- IP-Adresse und Port der Homematic-CCU (typischerweise Port 2010)
- Geräte-ID-Zuordnung für bis zu 6 Sensor- oder Aktorkanäle
- Auswahl des Wertetyps (z. B. ACTUAL_TEMPERATURE, STATE, POWER)
- Beschriftungen und Aktualisierungsintervalle für die Anzeige
- WLAN-Zugangsdaten und Netzwerkparameter

<p align="center">
  <img src="{{ '/assets/images/inki-homematic_webinterface.png' | relative_url }}" alt="Homematic Weboberfläche Hauptkonfiguration" width="300" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Detailansicht: XML-RPC-Abfrageparameter und Sensor-Mapping für Homematic-Geräte
</p>

**Setup-Schritte:**
1. Mit dem inki-Setup-WLAN verbinden
2. Weboberfläche unter http://192.168.4.1 aufrufen
3. Eigenes WLAN konfigurieren
4. IP-Adresse und Port der Homematic-CCU hinterlegen
5. Geräte-IDs und Wertetypen aus dem Homematic-System auswählen
6. Beschriftungen und Aktualisierungsintervalle festlegen und speichern

**XML-RPC-Integration:**
Das System nutzt Standard-Homematic-XML-RPC-Aufrufe wie:
```xml
<methodCall>
<methodName>getValue</methodName>
<params>
<param><value><string>002820C98F3853:2</string></value></param>
<param><value><string>ACTUAL_TEMPERATURE</string></value></param>
</params>
</methodCall>
```

## Technische Umsetzung

- **Protokoll**: XML-RPC-Direktanbindung an die Homematic-CCU (Port 2010)
- **Energiemanagement**: Vollständiges Abschalten zwischen geplanten Abfragen
- **Weck-/Schlafzyklus**: RTC-gesteuerte Aktivierung nach Zeitplan
- **Datenverarbeitung**: XML-Parsing in Echtzeit, optimiert für ePaper
- **Batterielaufzeit**: Mehrjährige Laufzeit abhängig vom Intervall
- **Netzwerk**: Automatische WLAN-Verbindung und CCU-Synchronisation
- **Servicemeldungen**: Automatisches Abholen und Anzeigen von CCU-Warnungen

{% include contact_cta_de.md %}
