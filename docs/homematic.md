---
layout: default
title: Homematic - inki
---

[← Back to homepage]({{ '/' | relative_url }})

**Home automation display** - inki-homematic

<p align="center">
  <img src="{{ '/assets/images/inki-homematic_3.JPG' | relative_url }}" alt="Homematic home automation display" width="500" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Homematic sensor data displayed on 4.2" ePaper showing temperature, heating status, and service messages
</p>

## Overview
Home automation systems collect a wealth of real-time data from sensors and devices throughout your home. While you can always check this information through web interfaces or mobile apps, there's something uniquely convenient about having key information automatically displayed on a dedicated "electronic dashboard" that you can glance at during your daily routine. Above you see inki displaying live data from our Homematic system - temperatures from HmIP-STE2-PCB sensors, heating control status from HmIP-WTH-1, and power monitoring from HmIP-PSM-2 devices.

Inki-homematic directly connects to your Homematic CCU via XML-RPC, retrieving and displaying live sensor data and device status information. It integrates seamlessly with existing Homematic installations to provide wireless ePaper visualization of your home automation data. In general any homematic device parameter can be read out by configuring inki via the webinterface, see the examples below.

<p align="center">
  <iframe src="https://makertube.net/videos/embed/bWefBKAkkHVg7govr5T3QV" allowfullscreen="" sandbox="allow-same-origin allow-scripts allow-popups" width="560" height="315" frameborder="0" style="border-radius: 8px; margin: 15px 0;">
  </iframe>
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Live demonstration of inki-homematic displaying real-time sensor data and updates
</p>

## Examples

- **Temperature monitoring** from HmIP-STE2-PCB sensors with real-time display
- **Heating system control** showing target and actual temperatures from HmIP-WTH-1
- **Power monitoring** displaying switch states and consumption from HmIP-PSM-2
- **XML-RPC integration** for direct CCU communication without additional software
- **Service message display** showing system alerts and device status information
- **Ultra-low power operation** with ~10,000 queries per battery set
- **Wireless configuration** via web interface with automatic updates

## Configuration & Setup

You can easily configure inki-homematic through the web interface, such as from your smartphone. Here's what the configuration screens look like:

<p align="center">
  <img src="{{ '/assets/images/inki-homematic_webinterface2.png' | relative_url }}" alt="Homematic web interface device configuration" width="300" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Main configuration screen 
</p>

Specific homematic settings are accessed by the corresponding page and allow to set the relevant parameters:

**Web Interface Setup:**
- Homematic CCU IP address and port configuration (typically port 2010)
- Device ID mapping for up to 6 sensor/actuator data points
- Value type selection (ACTUAL_TEMPERATURE, STATE, POWER, etc.)
- Display labels and update interval scheduling
- WiFi credentials and network settings

<p align="center">
  <img src="{{ '/assets/images/inki-homematic_webinterface.png' | relative_url }}" alt="Homematic web interface main configuration" width="300" style="border-radius: 8px; margin: 10px 0 5px 0;">
</p>
<p style="text-align: center; font-style: italic; color: #666; margin-top: 0; margin-bottom: 15px;">
Detailed device configuration showing XML-RPC query parameters and sensor mappings for homematic
</p>

**Setup Process:**
1. Connect to the inki setup WiFi network
2. Access the web interface at http://192.168.4.1
3. Configure your home WiFi credentials
4. Enter your Homematic CCU IP address and port
5. Add device IDs and value types from your Homematic system
6. Set display labels and update intervals

**XML-RPC Integration:**
The system uses standard Homematic XML-RPC calls like:
```xml
<methodCall>
<methodName>getValue</methodName>
<params>
<param><value><string>002820C98F3853:2</string></value></param>
<param><value><string>ACTUAL_TEMPERATURE</string></value></param>
</params>
</methodCall>
```

## Technical Implementation

- **Protocol**: XML-RPC direct integration with Homematic CCU (port 2010)
- **Power Management**: Complete power-down between scheduled updates
- **Wake/Sleep Cycle**: RTC-controlled activation at configurable intervals
- **Data Processing**: Real-time XML parsing optimized for ePaper display
- **Battery Life**: Years of operation depending on update frequency
- **Network**: Automatic WiFi connection and CCU synchronization
- **Service Monitoring**: Automatic retrieval and display of CCU service messages

<p align="center">
<strong>Questions? Ideas? Interested in a kit?</strong><br>
<strong>Contact:</strong> <a href="mailto:c0de@posteo.de">c0de@posteo.de</a>
</p>
