# Pico W Cores and Wi‑Fi (Short Guide)

This firmware links `pico_cyw43_arch_lwip_threadsafe_background`, which runs the CYW43 Wi‑Fi driver and lwIP TCP/IP stack in a background worker on core 1. Your application logic runs on core 0. You don’t need to manage the second core explicitly — it “just works”.

## Big Picture

Shared memory map (simplified):

```
External QSPI Flash (2 MB, XIP) — shared by both cores
+----------------------+  (program storage)
| .text/.rodata        |  executed-in-place via 16 KB XIP cache
| firmware slots/assets|
+----------------------+

On‑chip SRAM (264 KB) — shared by both cores
+----------------------+ high addr
| Stack (core 1)       |  CYW43/lwIP worker
+----------------------+
| Free headroom        |
+----------------------+
| Heap (global)        |  malloc/new for both cores
+----------------------+
| .bss/.data/globals   |
+----------------------+ low addr
```

Notes:
- Both cores share the same address space (flash and SRAM).
- Each core has its own stack; there is one global C heap used by both cores.
- USB DPRAM and XIP cache RAM are dedicated to peripherals, not general heap.

## Architecture Choice (Background, Thread‑Safe)

- Linked target: `pico_cyw43_arch_lwip_threadsafe_background` (see `CMakeLists.txt`).
- Consequences:
  - Core 1 runs the Wi‑Fi/driver and lwIP event loop.
  - Core 0 runs your app code; networking is serviced asynchronously.
  - `cyw43_arch_poll()` is NOT required (only for poll architectures).

## What Happens in Our Code

1) Wi‑Fi connect (core 0 initiates; core 1 performs)
- File: `wifi.c`, function: `wifi_connect()`
  - `cyw43_arch_init_with_country(...)` starts the background worker on core 1.
  - `cyw43_arch_enable_sta_mode()` configures STA on the WL chip.
  - `cyw43_arch_wifi_connect_timeout_ms(...)` blocks core 0 until core 1 finishes the join/DHCP.
  - On success we log RSSI via `wifi_log_rssi()`.

2) AP setup webserver (callbacks are driven by core 1)
- File: `main.c`, function: `enter_wifi_setup_mode()`
  - `cyw43_arch_enable_ap_mode(...)` brings up AP on core 1.
  - `start_setup_webserver()` sets up lwIP TCP listen; `accept_cb`/`recv_cb` in `webserver.c` are invoked by the background worker.

3) HTTP client fetch (core 0 requests; core 1 drives TCP)
- File: `http_client.c`
  - Core 0 builds request and calls connect/write.
  - Core 1 runs TCP state machine; delivers pbufs to `http_recv_callback()` where we parse headers/body.

## Practical Tips

- Keep network callbacks fast and non‑blocking; they run under the background worker context.
- Don’t call `cyw43_arch_deinit()` from within lwIP/CYW43 callbacks.
- Shared data between callbacks and app code should be synchronized if modified concurrently.
- Device Status web runs only in AP setup mode; STA RSSI is N/A there. We log RSSI on STA connect for diagnostics.

## TL;DR

- App on core 0; Wi‑Fi/lwIP on core 1; shared flash and SRAM.
- No `cyw43_arch_poll()` needed with the background arch.
- Your code stays simple; the second core handles radio and TCP/IP under the hood.

