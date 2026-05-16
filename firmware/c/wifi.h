#ifndef WIFI_H
#define WIFI_H

#include "lwip/ip_addr.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WIFI_SUCCESS = 0,
    WIFI_ERROR_CONNECTION = 1,
} wifi_result_t;

// Return a pointer to the device's 6-byte STA MAC. Cached after first read.
// wifi_connect() and wifi_ap_start() populate the cache for free (chip already
// up); if neither has run this wake, the first call cold-inits CYW43 (~1 s).
const uint8_t *wifi_mac(void);

// Read current link RSSI in dBm. Returns false if the link is down.
bool wifi_get_rssi(int32_t *out_dbm);

// Background poll (no-op on threadsafe_background, kept for call-site clarity).
void wifi_poll(void);

// Connect to Wi-Fi using flash-stored configuration.
// Returns a `wifi_result_t`; on success, station mode is enabled and connected.
wifi_result_t wifi_connect(void);

// Simple helper to log current Wi‑Fi RSSI (dBm) if connected; otherwise logs N/A.
void wifi_log_rssi(void);

// Shut down the Wi-Fi subsystem.
void wifi_deinit(void);

// Initialise CYW43 and start AP mode with WPA2-PSK. Caller must not have
// called cyw43_arch_init yet. Returns false if CYW43 init fails.
bool wifi_ap_start(const char *ssid, const char *password);

// Resolve hostname to IPv4 address using lwIP DNS (synchronous, polling).
// Must be called after wifi_connect(). Returns true on success.
bool wifi_resolve_hostname(const char *hostname, ip_addr_t *out_ip, int timeout_ms);

#endif // WIFI_H
