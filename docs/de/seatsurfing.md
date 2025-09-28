---
layout: default
title: Seatsurfing - inki
---

[← Zurück zur Startseite]({{ '/de/' | relative_url }})

**Raumbuchung und Desk-Sharing-Display** - inki-seatsurfing

<p align="center">
  <img src="{{ '/assets/images/inki_front.JPG' | relative_url }}" alt="Seatsurfing Raumbuchungs-Display" width="500" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Seatsurfing-Raumbuchungsdisplay mit Echtzeit-Verfügbarkeitsanzeige
</p>

## Überblick
Desk-Sharing ist in vielerlei Hinsicht ein gutes und modernes Konzept. Es bringt aber auch Herausforderungen mit sich: Man weiß nicht, wo man Kollegen findet und sucht durch Büros. Herkömmliche Papierschilder an Türen funktionieren nicht. Das löst inki.

Oben siehst du inki mit Live-Raumbuchungsdaten aus einem [Seatsurfing](https://github.com/seatsurfing/backend)-System - wie Raumverfügbarkeit und Belegungsinformationen. Das drahtlose ePaper-Display bietet eine kabellose, einfach an der Wand montierbare Lösung, die sich automatisch über WLAN aktualisiert.

Inki-seatsurfing verbindet sich direkt über die REST-API mit Seatsurfing-Servern über das "Service Account" und ruft Echtzeit-Raumbuchungs- und Desk-Sharing-Informationen ab und zeigt sie an.

<p align="center">
  <iframe src="https://makertube.net/videos/embed/gxEmY74gfjZvuuTyGfTvus" allowfullscreen="" sandbox="allow-same-origin allow-scripts allow-popups" width="560" height="315" frameborder="0" style="border-radius: 8px; margin: 15px 0;">
  </iframe>
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Live-Demonstration: inki-seatsurfing zeigt Echtzeit-Raumbuchungsdaten und Statusupdates
</p>

## Funktionen

- **Echtzeit-Raumverfügbarkeit** mit Belegungs-/Freistatus und Buchungsinformationen
- **Seatsurfing-REST-API-Integration** für direkte Serverkommunikation ohne Zusatzsoftware
- **Individuelle Logo-Anzeige** mit konfigurierbarem Branding über die Weboberfläche
- **Extrem niedriger Energiebedarf** mit rund 10.000 Abfragen pro Batteriesatz
- **Drahtlose Konfiguration** über Weboberfläche mit automatischen Updates
- **Kabellose Wandmontage** perfekt für Türbeschilderung und Desk-Sharing-Bereiche

## Konfiguration & Setup

Die Einrichtung erfolgt bequem über die Weboberfläche, sobald dein inki-Gerät läuft. So sehen die Konfigurationsseiten aus:

<p align="center">
  <img src="{{ '/assets/images/inki_webinterface_landingpage.png' | relative_url }}" alt="Seatsurfing Weboberfläche Startseite" width="300" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Hauptseite mit verfügbaren Konfigurationsoptionen für inki
</p>

**Webinterface-Einstellungen:**
- Seatsurfing-Service-Account-Zugangsdaten (aus Seatsurfing-Admin kopieren und einfügen)
- Orts-ID-Zuordnung für bestimmte Räume oder Arbeitsplatzbereiche
- Individuelle Logo-Upload über Weboberfläche
- Aktualisierungsintervalle planen (optimiert für Batterielaufzeit)
- WLAN-Zugangsdaten und Netzwerkeinstellungen

<p align="center">
  <img src="{{ '/assets/images/esign_4_2_webinterface.JPG' | relative_url }}" alt="Seatsurfing detaillierte Konfigurationsoberfläche" width="300" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Detailkonfiguration mit Seatsurfing-Integrationsparametern und Raumeinstellungen
</p>

**Setup-Schritte:**
1. Mit dem inki-Setup-WLAN verbinden
2. Weboberfläche unter http://192.168.4.1 aufrufen
3. Eigenes WLAN konfigurieren
4. Seatsurfing-Serverdetails und Service-Account-Zugangsdaten eingeben
5. Orts-IDs für anzuzeigende Räume oder Arbeitsplätze auswählen
6. Individuelle Logos hochladen und Aktualisierungsintervalle festlegen

**REST-API-Integration:**
Das System nutzt Seatsurfings Standard-REST-API, um Echtzeit-Buchungsinformationen, Raumverfügbarkeitsstatus und Belegungsdetails für nahtlose Integration in bestehende Desk-Sharing-Workflows abzurufen.

## Bewährte Anwendungsfälle

- **Besprechungsraum-Displays**: An Türen montierte Displays mit aktueller Verfügbarkeit und anstehenden Buchungen
- **Desk-Sharing-Bereiche**: Statusanzeigen für Hot-Desking-Umgebungen, die zeigen, wer wo ist
- **Büronavigation**: Kollegen helfen, sich in flexiblen Arbeitsplatzumgebungen zu finden
- **Individuelles Branding**: Corporate-Logo-Integration für professionelles Erscheinungsbild
- **Facility-Management**: Echtzeit-Arbeitsplatz-Auslastungsüberwachung
- **Besucherinformation**: Klare Statusanzeige für Gäste und externe Besprechungsteilnehmer

## Technische Umsetzung

- **Protokoll**: REST-API-Integration mit Seatsurfing-Servern
- **Energiemanagement**: Vollständiges Abschalten zwischen geplanten Updates
- **Weck-/Schlafzyklus**: RTC-gesteuerte Aktivierung in konfigurierbaren Intervallen
- **Datenverarbeitung**: JSON-Parsing optimiert für ePaper-Display
- **Batterielaufzeit**: Mehrjährige Laufzeit abhängig vom Update-Intervall
- **Netzwerk**: Automatische WLAN-Verbindung und Seatsurfing-Synchronisation
- **Authentifizierung**: Sichere Service-Account-Zugangsdatenverwaltung

{% include contact_cta_de.md %}
