/**
 * @file    wifi.c
 * @brief   Implementation of Wi-Fi functionality for the eSign project.
 *
 * Provides global variables and configurations for Wi-Fi connection,
 * including country settings, authentication mode, and MAC address storage.
 */

#include "wifi.h"
#include "debug.h"
#include "flash.h"
#include "pico/cyw43_arch.h" // Pico SDK header for Wi-Fi country and auth definitions
#include "hardware/watchdog.h"
#include "lwip/netif.h"

/**
 * @brief   Country configuration for Wi-Fi.
 * @details Used to set the regulatory domain for Wi-Fi operation.
 *          Default is Germany (CYW43_COUNTRY_GERMANY).
 */
uint32_t country = CYW43_COUNTRY_GERMANY;

/**
 * @brief   Wi-Fi authentication mode.
 * @details Configured to use WPA2 mixed mode (CYW43_AUTH_WPA2_MIXED_PSK).
 */
uint32_t auth = CYW43_AUTH_WPA2_MIXED_PSK;

/**
 * @brief   MAC address of the Wi-Fi interface.
 * @details Populated during Wi-Fi initialization and used for network communication.
 */
uint8_t mac_address[6] = {0}; // Initialize with zeros

WifiResult wifi_connect(void) {
    debug_log_with_color(COLOR_BOLD_GREEN, "Initializing Wi-Fi...\n");

    if (cyw43_arch_init_with_country(country)) {
        debug_log_with_color(COLOR_RED, "Wi-Fi initialization failed.\n");
        return WIFI_ERROR_CONNECTION;
    }
    cyw43_arch_enable_sta_mode();

    // Optional: set mDNS/hostname if configured
    if (device_config_flash.data.roomname[0] != '\0') {
        netif_set_hostname(netif_default, device_config_flash.data.roomname);
    }

    debug_log("Attempting to connect to network...\n");
    int wifi_connected = -1;
    int wifi_attempt_count = 0;
    while (wifi_connected != 0 && wifi_attempt_count < device_config_flash.data.number_wifi_attempts) {
        wifi_attempt_count++;
        wifi_connected = cyw43_arch_wifi_connect_timeout_ms(
            wifi_config_flash.ssid,
            wifi_config_flash.password,
            auth,
            device_config_flash.data.wifi_timeout
        );
        watchdog_update();
        debug_log_with_color(COLOR_YELLOW, "Trying to connect to %s ... Attempt %d\n",
                             wifi_config_flash.ssid, wifi_attempt_count);
    }

    if (wifi_connected != 0) {
        debug_log_with_color(COLOR_RED, "Failed to connect to Wi-Fi after %d attempts.\n", wifi_attempt_count);
        cyw43_arch_deinit();
        return WIFI_ERROR_CONNECTION;
    }

    debug_log("Connected to Wi-Fi successfully.\n");
    // Log RSSI after successful connection (useful for diagnostics)
    wifi_log_rssi();
    return WIFI_SUCCESS;
}

void wifi_log_rssi(void) {
    int link = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    if (link == CYW43_LINK_UP) {
        int rssi = cyw43_wifi_get_rssi(&cyw43_state);
        debug_log("Wi-Fi RSSI: %d dBm\n", rssi);
    } else {
        debug_log("Wi-Fi RSSI: N/A (not connected)\n");
    }
}
