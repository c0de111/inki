#include "wifi.h"
#define LOG_MODULE LOG_MOD_WIFI
#include "debug.h"
#include "flash.h"
#include "hardware/watchdog.h"
#include "lwip/dns.h"
#include "lwip/netif.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include <string.h>

static uint32_t country = CYW43_COUNTRY_GERMANY;
static uint32_t auth = CYW43_AUTH_WPA2_MIXED_PSK;

static uint64_t s_cyw43_on_us = 0;
static uint8_t s_mac[6] = {0};
static bool s_mac_valid = false;

// Read MAC from the live chip and cache it. Safe to call with chip up in
// either STA or AP mode — both MACs exist in the CYW43 regardless of mode.
static void wifi_capture_mac(void) {
    if (s_mac_valid)
        return;
    cyw43_arch_lwip_begin();
    int rc = cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, s_mac);
    cyw43_arch_lwip_end();
    if (rc == 0) {
        s_mac_valid = true;
        debug_status("OK", "MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", s_mac[0], s_mac[1], s_mac[2],
                     s_mac[3], s_mac[4], s_mac[5]);
    } else {
        debug_status("ERROR", "WiFi: MAC read failed\n");
    }
}

wifi_result_t wifi_connect(void) {
    dlog("Initializing Wi-Fi...\n");

    s_cyw43_on_us = time_us_64();
    if (cyw43_arch_init_with_country(country)) {
        debug_status("ERROR", "WiFi: CYW43 init failed\n");
        return WIFI_ERROR_CONNECTION;
    }
    debug_status("OK", "CYW43: chip on\n");
    cyw43_arch_enable_sta_mode();
    wifi_capture_mac();

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
                     (float)elapsed_ms / 1000.0f);
        cyw43_arch_deinit();
        return WIFI_ERROR_CONNECTION;
    }

    int32_t rssi = 0;
    wifi_get_rssi(&rssi);

    debug_status("OK", "WiFi: \"%s\" (connected in %.1fs, %d attempt%s, %ld dBm)\n",
                 wifi_config_flash.ssid, (float)elapsed_ms / 1000.0f, wifi_attempt_count,
                 wifi_attempt_count == 1 ? "" : "s", (long)rssi);
    return WIFI_SUCCESS;
}

bool wifi_get_rssi(int32_t *out_dbm) {
    if (!out_dbm)
        return false;
    *out_dbm = 0;
    bool ok = false;
    cyw43_arch_lwip_begin();
    int link = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    if (link >= CYW43_LINK_JOIN) {
        int32_t rssi = 0;
        if (cyw43_wifi_get_rssi(&cyw43_state, &rssi) == 0) {
            *out_dbm = rssi;
            ok = true;
        }
    }
    cyw43_arch_lwip_end();
    return ok;
}

void wifi_poll(void) { cyw43_arch_poll(); }

void wifi_log_rssi(void) {
    int32_t rssi = 0;
    if (wifi_get_rssi(&rssi)) {
        dlog("Wi-Fi RSSI: %ld dBm\n", (long)rssi);
    } else {
        dlog("Wi-Fi RSSI: N/A\n");
    }
}

void wifi_deinit(void) {
    uint32_t on_ms = (uint32_t)((time_us_64() - s_cyw43_on_us) / 1000);
    debug_status("OK", "CYW43: chip off (on for %.1fs)\n", (float)on_ms / 1000.0f);
    cyw43_arch_deinit();
}

bool wifi_ap_start(const char *ssid, const char *password) {
    if (cyw43_arch_init_with_country(country)) {
        debug_status("ERROR", "WiFi: CYW43 init failed (AP mode)\n");
        return false;
    }
    cyw43_arch_enable_ap_mode(ssid, password, CYW43_AUTH_WPA2_AES_PSK);
    wifi_capture_mac();
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

    // Stack-allocated; safe because dns_callback fires only inside cyw43_arch_poll(),
    // which is called from the loop below before this frame returns.
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

const uint8_t *wifi_mac(void) {
    if (s_mac_valid)
        return s_mac;

    // Cold path: chip is off. Init → STA mode → read → deinit.
    if (cyw43_arch_init_with_country(country)) {
        debug_status("ERROR", "WiFi: CYW43 init failed (MAC read)\n");
        return s_mac;
    }
    cyw43_arch_enable_sta_mode();
    wifi_capture_mac();
    cyw43_arch_deinit();
    return s_mac;
}
