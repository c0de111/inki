#ifndef WIFI_H
#define WIFI_H

#include "lwip/ip_addr.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @file    wifi.h
 * @brief   Definitions and declarations for Wi-Fi configuration and operations.
 *
 * Provides the interface for Wi-Fi settings, authentication modes,
 * and the result enumeration for communication outcomes.
 */

/**
 * @brief   Enum representing the result of Wi-Fi and server communication.
 *
 * Used to track the outcome of `wifi_server_communication` calls,
 * indicating success or the type of failure encountered.
 */
typedef enum {
    WIFI_SUCCESS = 0,          /**< Wi-Fi and server communication succeeded */
    WIFI_ERROR_CONNECTION = 1, /**< Wi-Fi connection failed */
    WIFI_ERROR_SERVER = 2,     /**< Server communication failed */
    WIFI_NOT_REQUIRED = 3      /**< Wi-Fi not required for this operation */
} WifiResult;

/**
 * @brief   Wi-Fi country configuration.
 * @details Defines the regulatory domain for Wi-Fi operations.
 *          The default value is set to Germany (CYW43_COUNTRY_GERMANY).
 */
extern uint32_t country;

/**
 * @brief   Wi-Fi authentication mode.
 * @details Configured to use WPA2 mixed mode (CYW43_AUTH_WPA2_MIXED_PSK) by default.
 */
extern uint32_t auth;

/**
 * @brief   MAC address buffer for the Wi-Fi interface.
 * @details The MAC address is populated during Wi-Fi initialization and
 *          used for network identification and communication.
 */
extern uint8_t mac_address[6];

// Read MAC address via CYW43 (initializes and deinitializes WiFi chip).
void read_mac_address(void);

// Connect to Wi-Fi using flash-stored configuration.
// Returns a `WifiResult`; on success, station mode is enabled and connected.
WifiResult wifi_connect(void);

// Simple helper to log current Wi‑Fi RSSI (dBm) if connected; otherwise logs N/A.
void wifi_log_rssi(void);

// Shut down the Wi-Fi subsystem.
void wifi_deinit(void);

// Initialise CYW43 and enable AP mode.  Caller must not have called cyw43_arch_init yet.
// Returns true on success, false if CYW43 init failed.
bool wifi_start_ap(void);

// Resolve hostname to IPv4 address using lwIP DNS (synchronous, polling).
// Must be called after wifi_connect(). Returns true on success.
bool wifi_resolve_hostname(const char *hostname, ip_addr_t *out_ip, int timeout_ms);

#endif // WIFI_H
