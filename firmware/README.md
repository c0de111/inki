# Firmware 

The **inki** firmware is a compact, bare-metal C application for the **Raspberry Pi Pico W**, designed for ultra-low-power **ePaper signage**. It supports "over-the-air" WIFI firmware updates, flash-persistent configuration, and dynamic content fetched via Wi-Fi from the Seatsurfing room booking system.

## Key Features

- No RTOS or dynamic memory  
  Runs on bare metal using the official Pico SDK, cyw43_arch and lwIP for Wi-Fi web server and client queries. Minimal runtime overhead, deterministic behavior, and low power consumption. Up to 10.000s refresh per battery life, tested in daily usage with 10.000s of operations without problem. 

- Dual-slot bootloader (Slot 0 and Slot 1)  
  Enables safe OTA / WIFI updates: new firmware is flashed to the inactive slot and activated only after verification by the device (magic word and crc32 check).

- Power-optimized architecture  
  A P-MOSFET fully disconnects power when inactive. Wakeups are triggered by RTC (DS3231) or pushbutton.

- Configuration in flash  
  Wi-Fi credentials, Seatsurfing settings, and display behavior are stored in dedicated flash regions, separate from firmware.

- Web-based configuration portal  
  Device features an access point mode (enter by holding all pushbuttons during startup). Configuration is done via browser (Wi-Fi, API, room, clock, ...).

- Seatsurfing API integration  
  Fetches live booking data and displays current room status. Multiple spaces per display supported.

- Flexible build system  
  Bootloader, lot-specific binaries and "factory"-setting binaries are generated automatically via `build.sh`, flashing is supported via `flash.sh`. 
hare 4.2″ or 7.5″ ePaper displays. The content persists without power.


---
# Building the eSign Firmware

Steps to clone, configure, and build the firmware.

## 1. Clone the repository

```bash
git clone https://github.com/your-org/inki.git
cd inki/firmware/c
```

## 2. Build
Run 

```bash
./build.sh
```
This should give you (among others) the following files (numbers show size):

```bash
46552   inki_bootloader.bin
 8332   inki_default_config.bin
680012  inki_slot0.bin
680012  inki_slot1.bin
```

## 3. Write the firmware

At least the very first time you have to write the bootloader, initial config and one firmware slot - see flash.sh. After initial flashing you can use the web interface. 

```bash
flash.sh
```

## Notes

- Make sure the correct version of the Pico SDK is properly included via `pico_sdk_import.cmake`.

---

## Debug Logging Sample Output for a single wake up cycle
[3849 ms] Trying to connect to ssid ... Attempt 1
[6642 ms] Trying to connect to ssid ... Attempt 2
[6643 ms] Connected to Wi-Fi successfully.
[6643 ms] Constructed HTTP Header:
GET /location/5230035c-94ce-4f3c-b112-ebc6afcb78b9/space/222c2a66-6e66-4825-8064-11a6c3ecf91f/availability HTTP/1.0
Host: seatsurfing.io
Authorization: Basic ZThjNTA4NzktasdYmQzZi00OTMtZDJjZjMyZDdlZTdmX2VzaWdauQHNlYXRzdXJmaW5fanLmxvY2FsOnQ2R1RCaHozY1dUN0JcFTDROSEVWVTJ0cWRNcDZnMnRu
d

[6644 ms] Data transmission in progress...
[6645 ms] 50 ms wait time for header/body #: [6695 ms]  [6745 ms]  [6795 ms]  [6845 ms]  [6896 ms]  [6946 ms]  [6996 ms]  [7046 ms]  [7096 ms]  [7146 ms]  [7197 ms]  [7247 ms]  [7297 ms]  [7347 ms]  [7397 ms]  [7447 ms]  [7498 ms]  [7548 ms]  [7598 ms]  [7648 ms]  [7698 ms]  [7749 ms]  [7799 ms]  [7849 ms]  [7899 ms]  [7949 ms]  [7999 ms]  [8050 ms]  [8100 ms]  [8150 ms]  [8200 ms]  [8250 ms]  [8300 ms]  [8350 ms]  [8401 ms]  [8451 ms]  [8501 ms]  [8551 ms]  [8601 ms]  [8651 ms]  [8702 ms]  [8752 ms]  [8802 ms]  [8852 ms]  [8902 ms]  [8952 ms]  [9003 ms]  [9053 ms]  [9103 ms]  [9153 ms]  [9203 ms]  [9253 ms]  [9304 ms]  [9354 ms]  [9404 ms]  [9454 ms]  [9505 ms]  [9555 ms]  [9567 ms] Buffer= HTTP/1.0 200 OK
Access-Control-Allow-Headers: *
Access-Control-Allow-Methods: POST, GET, PUT, DELETE, OPTIONS
Access-Control-Allow-Origin: *
Access-Control-Expose-Headers: X-Object-Id, X-Error-Code, Content-Length, Content-Type
Content-Type: application/json
Date: Sun, 03 Aug 2025 13:27:48 GMT
Content-Length: 338

[{"id":"222c2a66-6e66-4825-8064-11a6c3ecf91f","available":true,"locationId":"5230035c-94ce-4f3c-b112-ebc6afcb78b9","name":"Conference 1","x":990,"y":76,"width":204,"height":70,"rotation":0,"requireSubject":false,"attributes":null,"approverGroupIds":null,"allowedBookerGroupIds":null,"bookings":[],"allowed":true,"approvalRequired":false}]
[9605 ms]  [9605 ms] Parsed Content-Length: 338
[9606 ms] Received full JSON body (338 bytes)
[9608 ms] ✅ JSON response complete - Wi-Fi off.
[9609 ms] Disabling watchdog for ePaper setup...
[9609 ms] Initializing Waveshare 4.2-inch ePaper...
[12167 ms] Re-enabling watchdog...
[12167 ms] Creating new image...
[12168 ms] Selecting image...
[12169 ms] ePaper setup completed.
[12169 ms] render_page
[12174 ms] Flash-Logo gefunden: 104x95 px, 1235 bytes
[12184 ms] epaper_finalize_and_powerdown (display epaper page)...
[12184 ms] EPD_Display called for epaper type: 2
[14530 ms] Entering ePaper sleep mode for type: 2
[14930 ms] Shutting down the ePaper module...
[14930 ms] ...System shutting down.  
[14932 ms] Alarm2 set for 00:24 (RTC time)

---
## Display the doxygen html documentation

~/pico/esign/html $ python3 -m http.server 9999 &

---
