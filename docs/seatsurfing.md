---
layout: default
title: Seatsurfing - inki
---

[← Back to homepage]({{ '/' | relative_url }})

**Room booking and desk sharing display** - inki-seatsurfing

<p align="center">
  <img src="{{ '/assets/images/inki_front.JPG' | relative_url }}" alt="Seatsurfing room booking display" width="500" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Seatsurfing room booking display showing real-time seat availability
</p>

## Overview
Desk sharing is a good and modern concept in many ways. However, it also comes with challenges: You do not know where you'll find your colleague and start looking through offices. Traditional paper door signs do not work. This is solved by inki.

Above you see inki displaying live room booking data from [seatsurfing](https://github.com/seatsurfing/backend) - such as room availability and occupant information. The wireless ePaper display provides a cable-less, easily wall-mountable solution that updates automatically via WiFi.

Inki-seatsurfing connects directly to Seatsurfing servers via REST API via the "service account", retrieving and displaying real-time room booking and desk sharing information.

<p align="center">
  <iframe src="https://makertube.net/videos/embed/gxEmY74gfjZvuuTyGfTvus" allowfullscreen="" sandbox="allow-same-origin allow-scripts allow-popups" width="560" height="315" frameborder="0" style="border-radius: 8px; margin: 15px 0;">
  </iframe>
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Live demonstration of inki-seatsurfing displaying real-time room booking data and status updates
</p>

## Features

- **Real-time room availability** showing occupied/free status and booking information
- **Seatsurfing REST API integration** for direct server communication without additional software
- **Custom logo display** with configurable branding through web interface
- **Ultra-low power operation** with ~10,000 queries per battery set
- **Wireless configuration** via web interface with automatic updates
- **Cable-less wall mounting** no need to install power cables or network infrastructure - perfect for existing buildings

## Configuration & Setup


You can easily configure inki-homematic through the web interface, such as from your smartphone. Here's what the configuration screens look like:

<p align="center">
  <img src="{{ '/assets/images/inki_webinterface_landingpage.png' | relative_url }}" alt="Seatsurfing web interface landing page" width="300" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Main landing page showing available configuration options for inki
</p>

**Web Interface Setup:**
- Seatsurfing service account credentials (copy and paste from Seatsurfing admin)
- Location ID mapping for specific rooms or desk areas
- Custom logo upload through web interface
- Update interval scheduling (optimized for battery life)
- WiFi credentials and network settings

<p align="center">
  <img src="{{ '/assets/images/esign_4_2_webinterface.JPG' | relative_url }}" alt="Seatsurfing detailed configuration interface" width="300" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Detailed configuration screen showing Seatsurfing integration parameters and room settings
</p>

**Setup Process:**
1. Connect to the inki setup WiFi network
2. Access the web interface at http://192.168.4.1
3. Configure your home WiFi credentials
4. Enter your Seatsurfing server details and service account credentials
5. Select location IDs for the rooms or desks to display
6. Upload custom logos and set update intervals

## Technical Implementation

- **Protocol**: REST API integration with Seatsurfing servers
- **Power Management**: Complete power-down between scheduled updates
- **Wake/Sleep Cycle**: RTC-controlled activation at configurable intervals
- **Data Processing**: JSON parsing optimized for ePaper display
- **Battery Life**: Years of operation depending on update frequency
- **Network**: Automatic WiFi connection and Seatsurfing synchronization
- **Authentication**: Service account credential management

{% include contact_cta_en.md %}
