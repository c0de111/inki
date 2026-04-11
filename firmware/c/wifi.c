/**
 * @file    wifi.c
 * @brief   Implementation of Wi-Fi functionality for the eSign project.
 *
 * Provides global variables and configurations for Wi-Fi connection,
 * including country settings, authentication mode, and MAC address storage.
 */

#include "wifi.h"
#define LOG_MODULE LOG_MOD_WIFI
#include "debug.h"
#include "flash.h"
#include "hardware/watchdog.h"
#include "lwip/dns.h"
#include "lwip/netif.h"
#include "pico/cyw43_arch.h" // Pico SDK header for Wi-Fi country and auth definitions
#include "pico/stdlib.h"
#include <string.h>

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

static uint64_t s_cyw43_on_us = 0;

WifiResult wifi_connect(void) {
    dlog("Initializing Wi-Fi...\n");

    s_cyw43_on_us = time_us_64();
    if (cyw43_arch_init_with_country(country)) {
        debug_status("ERROR", "WiFi: CYW43 init failed\n");
        return WIFI_ERROR_CONNECTION;
    }
    debug_status("OK", "CYW43: chip on\n");
    cyw43_arch_enable_sta_mode();

    if (device_config_flash.data.roomname[0] != '\0') {
        netif_set_hostname(netif_default, device_config_flash.data.roomname);
    }

    uint64_t t0 = time_us_64();
    int wifi_connected = -1;
    int wifi_attempt_count = 0;
    while (wifi_connected != 0 &&
           wifi_attempt_count < device_config_flash.data.number_wifi_attempts) {
        wifi_attempt_count++;
        wifi_connected =
            cyw43_arch_wifi_connect_timeout_ms(wifi_config_flash.ssid, wifi_config_flash.password,
                                               auth, device_config_flash.data.wifi_timeout);
        watchdog_update();
        dlog("Trying to connect to %s ... Attempt %d\n", wifi_config_flash.ssid,
             wifi_attempt_count);
    }
    uint32_t elapsed_ms = (uint32_t)((time_us_64() - t0) / 1000);

    if (wifi_connected != 0) {
        debug_status("ERROR", "WiFi: failed after %d attempts (%.1fs)\n", wifi_attempt_count,
                     elapsed_ms / 1000.0f);
        cyw43_arch_deinit();
        return WIFI_ERROR_CONNECTION;
    }

    // Get RSSI for status line
    int32_t rssi = 0;
    cyw43_arch_lwip_begin();
    cyw43_wifi_get_rssi(&cyw43_state, &rssi);
    cyw43_arch_lwip_end();

    debug_status("OK", "WiFi: \"%s\" (connected in %.1fs, %d attempt%s, %ld dBm)\n",
                 wifi_config_flash.ssid, elapsed_ms / 1000.0f, wifi_attempt_count,
                 wifi_attempt_count == 1 ? "" : "s", (long)rssi);
    return WIFI_SUCCESS;
}

void wifi_log_rssi(void) {
    // With thread-safe background arch, guard driver calls
    cyw43_arch_lwip_begin();
    int link = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    // Treat JOIN and NOIP as connected-enough to fetch RSSI; negatives are error states
    if (link >= CYW43_LINK_JOIN) {
        int32_t rssi = 0;
        if (cyw43_wifi_get_rssi(&cyw43_state, &rssi) == 0) {
            cyw43_arch_lwip_end();
            dlog("Wi-Fi RSSI: %ld dBm\n", (long)rssi);
            return;
        } else {
            cyw43_arch_lwip_end();
            dlog("Wi-Fi RSSI: unknown (driver error)\n");
            return;
        }
    }
    cyw43_arch_lwip_end();
    dlog("Wi-Fi RSSI: N/A (link=%d)\n", link);
}

void wifi_deinit(void) {
    uint32_t on_ms = (uint32_t)((time_us_64() - s_cyw43_on_us) / 1000);
    debug_status("OK", "CYW43: chip off (on for %.1fs)\n", on_ms / 1000.0f);
    cyw43_arch_deinit();
}

bool wifi_start_ap(void) {
    if (cyw43_arch_init_with_country(country)) {
        debug_status("ERROR", "WiFi: CYW43 init failed (AP mode)\n");
        return false;
    }
    return true;
}

typedef struct {
    volatile bool done;
    bool success;
    ip_addr_t ip;
} dns_result_t;

static void dns_callback(const char *name, const ip_addr_t *ipaddr, void *arg) {
    (void)name;
    dns_result_t *res = (dns_result_t *)arg;
    if (ipaddr) {
        res->ip = *ipaddr;
        res->success = true;
    }
    res->done = true;
}

bool wifi_resolve_hostname(const char *hostname, ip_addr_t *out_ip, int timeout_ms) {
    if (!hostname || !out_ip)
        return false;

    dns_result_t result = {.done = false, .success = false};

    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(hostname, &result.ip, dns_callback, &result);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) {
        // Cache hit — result already populated
        *out_ip = result.ip;
        dlog("[WIFI] DNS cache hit: %s\n", hostname);
        return true;
    }
    if (err != ERR_INPROGRESS) {
        dlog("[WIFI] DNS error %d for %s\n", (int)err, hostname);
        return false;
    }

    // Poll until callback fires or timeout
    uint64_t deadline = time_us_64() + (uint64_t)timeout_ms * 1000;
    while (!result.done && time_us_64() < deadline) {
        sleep_ms(10);
        watchdog_update();
    }

    if (!result.done || !result.success) {
        dlog("[WIFI] DNS timeout/failure for %s\n", hostname);
        return false;
    }

    *out_ip = result.ip;
    dlog("[WIFI] DNS resolved: %s\n", hostname);
    return true;
}

void read_mac_address(void) {
    memset(mac_address, 0, sizeof(mac_address));

    if (cyw43_arch_init_with_country(country)) {
        debug_status("ERROR", "WiFi: CYW43 init failed (MAC read)\n");
        return;
    }

    cyw43_arch_enable_sta_mode();

    if (cyw43_wifi_get_mac(&cyw43_state, 0, mac_address) != 0) {
        debug_status("ERROR", "WiFi: MAC read failed\n");
        cyw43_arch_deinit();
        return;
    }

    debug_status("OK", "MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac_address[0], mac_address[1],
                 mac_address[2], mac_address[3], mac_address[4], mac_address[5]);

    cyw43_arch_deinit();
}
