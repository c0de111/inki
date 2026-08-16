# Tinta firmware

Bare-metal C firmware for the Raspberry Pi Pico W. Supports multiple use cases selected at build time.

## Building

```bash
./build.sh                # default (SeatSurfing)
./build.sh --seatsurfing  # room booking display
./build.sh --historian    # time-series visualization
./build.sh --weathermap   # weather map overlay
./build.sh --help         # show all options
```

Requires Pico SDK 2.1.0 (`PICO_SDK_PATH`), ARM toolchain, CMake 3.12+.

## Flashing

```bash
./flash.sh   # SWD flash — check which sections are enabled first!
./reset.sh   # hardware reset
```

**Always verify** which flash sections are active before flashing:
```bash
grep -n "^echo.*Flash" flash.sh
```

## NFC Integration (ST25DV)

The ST25DV04KC NFC tag allows the Android app to wake Tinta and send commands via EEPROM.

### Protocol

16-byte INKI request at EEPROM 0x01F0:
```
"INKI" (4) | version (1) | opcode (1) | duration_min (2 LE) | unix_seconds (4 LE) | nonce (4 LE)
```

Opcodes: `0x11` Page 0, `0x12` Decision Maker, `0x20` Text Message (text payload at 0x0008).

### Boot Sequence

1. ST25 power on, driver init, read IT_STS_Dyn and RF field status
2. If phone detected (RF field ON or IT_STS != 0): wait 300ms, then read EEPROM with up to 5 retries
3. If no phone (phantom wake): single read, no delay — fast reject
4. Validate request (magic, version, nonce != 0, nonce != previous)
5. Configure GPO (after reads, to avoid I2C/RF conflicts during phone writes)

### Reliability Measures

The ST25DV GPO fires per-block (not per-transaction). Writing 16 bytes = 4 blocks = 4 GPO pulses. The first block wakes the Pico while the phone is still writing the remaining blocks. This creates race conditions with stale EEPROM data.

**Mitigations:**
- **300ms delay** before first EEPROM read when phone is present — lets all 4 blocks complete
- **Late clear at shutdown** — wipes any request written during the ePaper cycle, preventing stale data from persisting across boots
- **Nonce deduplication** — last-processed nonce saved to EEPROM 0x0004, rejects stale replays
- **GPO config after reads** — `configure_wake_gpo()` moved to after the read loop to avoid I2C writes that block RF during the phone's write window
- **10-second Android cooldown** — prevents phantom re-writes from NFC reader mode after the boot cycle completes

### Wake Source Decision Tree

```
request_valid?
├── YES → NFC wake → process opcode as soft pushbutton
└── NO
    ├── pushbutton != 0 → button wake → page selection
    ├── alarm_flag → RTC scheduled wake → page 0
    ├── it_sts != 0 → spurious NFC (no INKI payload) → blink + power off
    └── default → button 0 / power-on → page 0
```

## Architecture

- `main.c` — core application, wake logic, page rendering
- `st25_io.c/.h` — ST25DV boot check, EEPROM read/write, GPO config
- `http_client.c/.h` — HTTP/HTTPS client (SeatSurfing, Historian, telemetry)
- `webserver.c` — setup mode web interface
- `flash.c` — persistent configuration storage
- `ds3231.c/.h`, `rv3028.c/.h` — RTC drivers
- `third_party/` — ePaper drivers, fonts, GUI, ST25DV driver, cJSON
