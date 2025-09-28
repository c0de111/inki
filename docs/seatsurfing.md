---
layout: default
title: Seatsurfing - inki
---

[← Back to homepage](/)

**Room booking and desk sharing display** - inki-seatsurfing

<p align="center">
  <img src="{{ '/assets/images/esign_4_2_1_cropped.jpg' | relative_url }}" alt="Seatsurfing room booking display" width="400" style="border-radius: 8px; margin: 20px 0;">
</p>

## Overview

The Seatsurfing use case transforms inki into a smart room booking display that shows real-time availability and scheduling information for meeting rooms and desk spaces.

## Features

- **Live booking data** via REST API integration
- **Room availability status** with occupant names
- **Schedule information** for upcoming meetings
- **Custom logos and layouts** for branding
- **Multiple spaces per display** support

## Technical Details

- **API Integration**: Connects to Seatsurfing servers via REST API
- **Authentication**: Basic authentication with configurable credentials
- **Data Format**: JSON response parsing for booking information
- **Update Frequency**: Configurable refresh intervals
- **Display**: Room status, occupant names, and schedule information

## Configuration

The device configuration includes:
- Seatsurfing server credentials
- Location IDs for room mapping
- Refresh intervals
- Display layout preferences
