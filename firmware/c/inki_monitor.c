#include "inki_monitor.h"

#define LOG_MODULE LOG_MOD_MONITOR
#include "boot_input.h"
#include "debug.h"
#include "flash.h"
#include "http_client.h"
#include "sensors.h"
#include "use_case.h"
#include "version.h"
#include "wifi.h"

#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INKI_MONITOR_TIMEOUT_MS_DEFAULT 2000
#define INKI_MONITOR_TIMEOUT_MS_MIN 100
#define INKI_MONITOR_TIMEOUT_MS_MAX 10000
#define INKI_MONITOR_HOST_BUFSIZE TELEMETRY_HOST_MAX_LEN
#define INKI_MONITOR_HTTP_PATH "/api/v1/telemetry"

static int inki_monitor_sanitize_timeout_ms(int timeout_ms) {
    if (timeout_ms < INKI_MONITOR_TIMEOUT_MS_MIN || timeout_ms > INKI_MONITOR_TIMEOUT_MS_MAX) {
        return INKI_MONITOR_TIMEOUT_MS_DEFAULT;
    }
    return timeout_ms;
}

static const char *inki_monitor_room_type_name(RoomType type) {
    switch (type) {
    case ROOM_TYPE_OFFICE:
        return "office";
    case ROOM_TYPE_CONFERENCE:
        return "conference";
    case ROOM_TYPE_LAB:
        return "lab";
    case ROOM_TYPE_WORKSHOP:
        return "workshop";
    default:
        return "unknown";
    }
}

// Minimal JSON string escaping for labels/version strings.
static void json_escape_string(char *dst, size_t dst_size, const char *src) {
    size_t w = 0;
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (size_t i = 0; src[i] != '\0' && w + 1 < dst_size; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\\' || c == '"') {
            if (w + 2 >= dst_size) {
                break;
            }
            dst[w++] = '\\';
            dst[w++] = (char)c;
        } else if (c >= 0x20 && c <= 0x7E) {
            dst[w++] = (char)c;
        } else {
            dst[w++] = '_';
        }
    }
    dst[w] = '\0';
}

static bool inki_monitor_send_sample(const inki_monitor_sample_t *sample) {
    if (!sample) {
        return false;
    }

    // Telemetry is optional; disabled or incomplete config is treated as a quiet no-op.
    if (!device_config_flash.data.telemetry_enabled) {
        return true;
    }
    if (device_config_flash.data.telemetry_host[0] == '\0' ||
        device_config_flash.data.telemetry_port == 0 ||
        device_config_flash.data.telemetry_token[0] == '\0') {
        return true;
    }

    ip_addr_t server_ip;
    uint16_t server_port = device_config_flash.data.telemetry_port;
    char server_host[INKI_MONITOR_HOST_BUFSIZE];
    strncpy(server_host, device_config_flash.data.telemetry_host, sizeof(server_host) - 1);
    server_host[sizeof(server_host) - 1] = '\0';
    if (!ipaddr_aton(server_host, &server_ip)) {
        dlog("[INKI_MON] Invalid telemetry host (V1 expects IPv4 literal)\n");
        return false;
    }

    int timeout_ms =
        inki_monitor_sanitize_timeout_ms(device_config_flash.data.telemetry_timeout_ms);

    // Guard against accidental request overlap because http_request_async() resets the shared
    // session unconditionally.
    int idle_wait_ms = 0;
    while (http_session_is_active() && idle_wait_ms < 1000) {
        sleep_ms(10);
        watchdog_update();
        idle_wait_ms += 10;
    }
    if (http_session_is_active()) {
        dlog("[INKI_MON] HTTP session still active, skipping telemetry.\n");
        return false;
    }

    const uint8_t *mac = wifi_mac();
    char device_id[sizeof("inki-") + 12] = {0};
    snprintf(device_id, sizeof(device_id), "inki-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);

    int32_t rssi_dbm = 0;
    bool have_rssi = wifi_get_rssi(&rssi_dbm);

    char label_escaped[80];
    char version_escaped[80];
    char build_date_escaped[32];
    const char *label_src = (device_config_flash.data.telemetry_label[0] != '\0')
                                ? device_config_flash.data.telemetry_label
                                : (sample->label ? sample->label : "");
    json_escape_string(label_escaped, sizeof(label_escaped), label_src);
    json_escape_string(version_escaped, sizeof(version_escaped), version ? version : "");
    char build_date_short[16] = {0};
    if (build_date && build_date[0] != '\0') {
        snprintf(build_date_short, sizeof(build_date_short), "%.10s", build_date);
    }
    json_escape_string(build_date_escaped, sizeof(build_date_escaped), build_date_short);

    char rssi_fragment[64];
    if (have_rssi) {
        snprintf(rssi_fragment, sizeof(rssi_fragment), "\"wifi_rssi_dbm\":%ld,", (long)rssi_dbm);
    } else {
        snprintf(rssi_fragment, sizeof(rssi_fragment), "\"wifi_rssi_dbm\":null,");
    }

    char wake_fragment[64];
    if (sample->wake_source && sample->wake_source[0] != '\0') {
        char ws_escaped[32];
        json_escape_string(ws_escaped, sizeof(ws_escaped), sample->wake_source);
        snprintf(wake_fragment, sizeof(wake_fragment), "\"wake_source\":\"%s\",", ws_escaped);
    } else {
        wake_fragment[0] = '\0';
    }

    char json_body[768];
    int json_len =
        snprintf(json_body, sizeof(json_body),
                 "{"
                 "\"device_id\":\"%s\","
                 "\"label\":\"%s\","
                 "\"query_ok\":%s,"
                 "%s"
                 "%s"
                 "\"fw_version\":\"%s\","
                 "\"fw_build_date\":\"%s\","
                 "\"room_type\":\"%s\","
                 "\"use_case\":\"%s\","
                 "\"metrics\":{"
                 "\"telemetry_send_elapsed_ms\":%lu,"
                 "\"battery_before_wifi_v\":%.3f,"
                 "\"battery_after_wifi_v\":%.3f,"
                 "\"coin_cell_v\":%.3f,"
                 "\"pico_temp_c\":%.2f"
                 "}"
                 "}",
                 device_id, label_escaped, sample->query_ok ? "true" : "false", rssi_fragment,
                 wake_fragment, version_escaped, build_date_escaped,
                 inki_monitor_room_type_name(device_config_flash.data.type), use_case.name,
                 (unsigned long)sample->telemetry_send_elapsed_ms,
                 (double)sample->battery_before_wifi_v, (double)sample->battery_after_wifi_v,
                 (double)sample->coin_cell_v, (double)sample->pico_temp_c);

    if (json_len <= 0 || (size_t)json_len >= sizeof(json_body)) {
        dlog("[INKI_MON] JSON body build failed/truncated.\n");
        return false;
    }

    char request[HTTP_REQUEST_MAX];
    int req_len = snprintf(request, sizeof(request),
                           "POST " INKI_MONITOR_HTTP_PATH " HTTP/1.1\r\n"
                           "Host: %s:%u\r\n"
                           "Authorization: Bearer %s\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           server_host, (unsigned)server_port,
                           device_config_flash.data.telemetry_token, json_len, json_body);
    if (req_len <= 0 || (size_t)req_len >= sizeof(request)) {
        dlog("[INKI_MON] HTTP request build failed/truncated.\n");
        return false;
    }

    if (!http_request_sync_count_only(&server_ip, server_port, request, timeout_ms)) {
        dlog("[INKI_MON] Telemetry request failed\n");
        return false;
    }

    dlog("[INKI_MON] Telemetry sent: %s U=%.3fV->%.3fV coin=%.3fV T=%.1fC telemetry=%lums "
         "query_ok=%d\n",
         device_id, (double)sample->battery_before_wifi_v, (double)sample->battery_after_wifi_v,
         (double)sample->coin_cell_v, (double)sample->pico_temp_c,
         (unsigned long)sample->telemetry_send_elapsed_ms, sample->query_ok ? 1 : 0);
    return true;
}

void inki_monitor_send_telemetry(float battery_before_v, float coin_cell_v, bool query_ok) {
    const char *label =
        device_config_flash.data.roomname[0] ? device_config_flash.data.roomname : NULL;
    inki_monitor_sample_t sample = {
        .battery_before_wifi_v = battery_before_v,
        .battery_after_wifi_v = read_battery_voltage(device_config_flash.data.conversion_factor),
        .coin_cell_v = coin_cell_v,
        .pico_temp_c = read_onchip_temperature_c(),
        .telemetry_send_elapsed_ms = (uint32_t)to_ms_since_boot(get_absolute_time()),
        .query_ok = query_ok,
        .label = label,
        .wake_source = wake_source,
    };
    (void)inki_monitor_send_sample(&sample);
}
