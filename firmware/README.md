# Firmware 

The eSign firmware is a compact C-based application for the **Raspberry Pi Pico W**, designed for low-power operation and efficient room signage via Wi-Fi and ePaper display.

## Key Features

- **RTC-Controlled Wakeup & Power Cycling**  
  The DS3231 real-time clock triggers periodic wakeups or user-initiated ones via pushbutton. A MOSFET-based gate fully disconnects the Pico W from power when idle — saving more energy than traditional sleep modes.

- **Wi-Fi Connectivity**  
  During wakeup, the Pico W connects to a Wi-Fi network (credentials stored in `wifi_credentials.h`) and fetches booking data via HTTP from the Seatsurfing API (`seatsurfing_api.h`).

- **Seatsurfing API Integration**  
  Authenticates via static credentials and queries occupancy state of spaces defined in `rooms.h`. Responses are parsed and displayed accordingly.

- **Dynamic Page Rendering**  
  The firmware supports multiple display pages, selectable via pushbuttons at startup. 

- **ePaper Display Control**  
  Uses SPI to render static screen updates on Waveshare 4.2″ or 7.5″ ePaper displays. The content persists without power.

- **Battery Voltage Monitoring**  
  Voltage of both the main (AAA/AA) and backup (coin cell) power sources is monitored via switchable voltage dividers and Pico ADC inputs — only active when needed.

- **No RTOS or dynamic memory**  
  Runs on bare metal using the official Pico SDK — minimal runtime overhead, deterministic behavior, and low binary size.

- **Build System**  
  The firmware is built using CMake and a custom `build.sh` script that allows per-room configurations (e.g., `ROOM=ROOM102H`).


---
# Building the eSign Firmware

Steps to clone, configure, and build the firmware.

## 1. Clone the repository

```bash
git clone "insert repository link here"
cd esign/firmware/c
```

## 2. Configure Wi-Fi, API access, and space mapping

Before building the firmware, configure:

### a) Wi-Fi credentials

```bash
cp wifi_credentials.h.example wifi_credentials.h
nano wifi_credentials.h
```

Set the following values:

```c
#define WIFI_SSID     "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
```

### b) Seatsurfing API settings

```bash
cp seatsurfing_api.h.example seatsurfing_api.h
nano seatsurfing_api.h
```
Create a (read-only) service account and use the credentials as displayed in the web interface. Set the following values according to your setup:

```c
// Seatsurfing-API-Server
#define API_HOST "seatsurfing.io" // your domain
#define API_IP   {192, 168, 1, 123} //  IP of the seatsurfing server
#define API_PORT 8080 // port of the seatsurfing server

// seatsurfing Service-Account
#define API_USER_ESIGN "xxxxxxxx-xxx-xxxx-xxxx-xxxxxxxxxxxx_xxxx@seatsurfing.local" // as displayed in the seatsurfing webinterface
#define API_PWD_ESIGN  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" // as displayed in the seatsurfing webinterface

```

### c) Room and space configuration

Edit in `rooms.h` for your room / eSign RoomConfig struct the room-specific space and location:

```c
.space_id = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",      // as displayed in the webinterface
.location_id = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"       // as displayed in the webinterface
```

## 3. Remove any old build cache

```bash
rm -rf build/
mkdir build
cd build
```

## 4. Configure the build for Pico W using CMake

```bash
cmake -DPICO_BOARD=pico_w ..
```

> This step sets the board target to `pico_w`, which is required for Wi-Fi support via the `cyw43_arch` library.

## 5. Build the firmware

```bash
ROOM=102H ./build.sh
```
Choose the room for your eSign build via the `ROOM=` argument in the build step by matching to the desired .roomname = "xxxxx" in rooms.h.


## 6. Write the firmware

```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -c "program build/esign.elf reset exit"
```

## Notes

- Make sure the correct version of the Pico SDK is properly included via `pico_sdk_import.cmake`.
- define the desired room configuration by setting `ROOM=...` when invoking the build script `build.sh`.

---


## Debug Logging Sample Output for a single wake up cycle

Buffered debug log:<br>
[1716 us] System initializing...<br>
[1896 us] watchdog_enable...<br>
[1944 us] ADC read...<br>
[2020 us] Battery voltage: 4.559 V<br>
[2270 us] hold power...<br>
[2310 us] Gate Pin on -> Power switch on<br>
[2338 us] init_clock...<br>
[2417 us] start setup_and_read_pushbuttons...<br>
[17 ms] wifi_server_communication...<br>
[17 ms] Wi-Fi required: Default page 0.<br>
[17 ms] Initialization of Wi-Fi [switching cyw43 module on]...<br>
[3657 ms] Trying to connect... Attempt 1<br>
[3658 ms] Connected to Wi-Fi successfully.<br>
[3658 ms] Data transmission in progress...<br>
[3658 ms] 50 ms wait time for response #: [3708 ms]  [3759 ms]  [3809 ms]  [3859 ms]  [3909 ms]  [3959 ms]  [4009 ms]  [4059 ms]  [4109 ms]  [4159 ms]  [4209 ms]  [4259 ms]  [4309 ms]  [4359 ms]  [4361 ms] Server response received successfully - WIFI off<br> [switching cyw43 module off].<br>
[4362 ms] Initializing Waveshare 7.5-inch V2 ePaper...<br>
[8948 ms] render_page...<br>
[8954 ms] QR code 2 overwritten successfully.<br>
[8985 ms] epaper_finalize_and_powerdown (display epaper page)...<br>
[13005 ms] Entering ePaper sleep mode...<br>
[13286 ms] ...System shutting down.<br>
---
## Display the doxygen html documentation

~/pico/esign/html $ python3 -m http.server 9999 &

---

## Project Configuration and Build System Workflow

This section outlines the approach for managing configuration, code structure, and build system organization for the project. The goal was to separate configuration from logic, improve maintainability and readability, and streamline the build process to dynamically select room configurations.


### 1. Separation of Configuration and Logic

#### Objective
- Isolate static configurations (e.g., room properties) from the main application logic to enhance modularity and reduce duplication.

#### Implementation
- Configuration: Room-specific configurations are defined in a dedicated `rooms.h` file.
- Logic: The main application logic in `main.c` accesses these configurations through a global pointer (`current_room`) pointing to the active configuration.

### 2. Use of Structs and Enums

#### Objective
- Provide a structured and scalable way to define room-specific properties and shared attributes.

#### Implementation
- Structs: Used for detailed room configurations (`RoomConfig`) and room type properties (`RoomTypeProperties`).
  - Example: `RoomConfig` includes details such as `roomname`, `number_wifi_attempts`, and `pushbutton1_pin`.
- Enums: Used for ePaper types (`EpaperType`) and room types (`RoomType`) to enhance readability and reduce reliance on hardcoded values.
  - Example: `ROOM_TYPE_OFFICE` for office spaces, `ROOM_TYPE_CONFERENCE` for conference rooms.

#### Advantages
- Adding a new room or room type requires minimal changes, improving scalability.
- Enums and structs enhance code clarity by using meaningful names instead of numeric values.

### 3. Defining Room Configurations

#### Objective
- Use room-specific configurations as compile-time constants to ensure they remain immutable and prevent runtime errors.

#### Implementation
- Each room's configuration (e.g., `113H_config`, `111H_config`) inherits from a campus-wide default (`DEFAULT_CAMPUS_H_CONFIG`) and overrides only the necessary fields.
- A global pointer `current_room` is set to the active room's configuration at compile time using preprocessor directives (`#ifdef`).
- Example Room Definition in `rooms.h` file.
