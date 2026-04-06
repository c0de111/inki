#include "seatsurfing/client.h"
#include "base64.h"
#define LOG_MODULE LOG_MOD_SEATSURFING
#include "debug.h"
#include "epaper_pages_shared.h"
#include "flash.h"
#include "http_client.h"
#include "lwip/ip_addr.h"
#include "pico/stdlib.h"
#include "seatsurfing/epaper_pages.h"
#include "st25_io.h"
#include "third_party/cjson/cJSON.h"
#include "use_case.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// --- Parsed data storage ---

seat_info_t seatsurfing_data[SEATSURFING_MAX_SEATS] = {0};

static seat_info_t seatsurfing_parse_seat_info(const char *json,
                                               const char *target_space_id_or_name);

// --- Name formatting helpers ---

static void format_name_from_email(const char *email, char *outbuf, size_t outbuf_len) {
    if (!email || !outbuf || outbuf_len < 2) {
        if (outbuf && outbuf_len > 0)
            outbuf[0] = '\0';
        return;
    }

    const char *at = strchr(email, '@');
    if (!at || at == email) {
        strncpy(outbuf, email, outbuf_len - 1);
        outbuf[outbuf_len - 1] = '\0';
        return;
    }

    size_t name_part_len = at - email;
    if (name_part_len >= outbuf_len)
        name_part_len = outbuf_len - 1;

    char name_part[64];
    strncpy(name_part, email, name_part_len);
    name_part[name_part_len] = '\0';

    char *dot = strchr(name_part, '.');
    if (dot)
        *dot = ' ';

    for (char *p = name_part; *p; ++p) {
        if (p == name_part || *(p - 1) == ' ') {
            *p = toupper(*p);
        } else {
            *p = tolower(*p);
        }
    }

    strncpy(outbuf, name_part, outbuf_len - 1);
    outbuf[outbuf_len - 1] = '\0';
}

static void abbreviate_name_if_needed(char *name, size_t max_chars) {
    size_t len = strlen(name);
    if (len <= max_chars)
        return;

    char *space = strrchr(name, ' ');
    if (!space || space == name || space[1] == '\0')
        return;

    space[1] = toupper((unsigned char)space[1]);
    space[2] = '.';
    space[3] = '\0';
}

void ss_format_seat(const seat_info_t *seat, char *out, size_t out_len, size_t max_chars) {
    if (seat->is_available) {
        strncpy(out, "frei", out_len);
    } else {
        format_name_from_email(seat->user_email, out, out_len);
        abbreviate_name_if_needed(out, max_chars);
    }
}

// --- HTTP response callback ---

static void seatsurfing_data_received(const char *response_data, size_t length, void *arg) {
    if (!response_data || length == 0) {
        debug_status("ERROR", "SeatSurfing: transfer failed or incomplete\n");
        return;
    }

    dlog("Received %d bytes of response data\n", (int)length);

    // Clamp seat count (currently render supports up to 3 on 7.5")
    uint8_t seat_count = seatsurfing_config_flash.data.seat_count;
    if (seat_count == 0)
        seat_count = 1;
    if (seat_count > 3)
        seat_count = 3;

    // Parse JSON for each configured seat (by name or ID)
    for (uint8_t i = 0; i < seat_count; i++) {
        const char *target = seatsurfing_config_flash.data.space_ids[i];
        seatsurfing_data[i] = seatsurfing_parse_seat_info(response_data, target);
    }

    debug_log("[SEATSURFING] Parsed %u seats (first avail=%s, user=%s)\n", seat_count,
              seatsurfing_data[0].is_available ? "YES" : "NO",
              seatsurfing_data[0].user_email[0] ? seatsurfing_data[0].user_email : "None");
}

// --- HTTP request builder ---

static int seatsurfing_build_http_request(char *buffer, size_t buffer_size, const char *host,
                                          const char *location_id, const char *auth_b64) {
    if (!buffer || !host || !location_id || !auth_b64) {
        return -1;
    }

    int len = snprintf(buffer, buffer_size,
                       "GET /location/%s/space/availability HTTP/1.0\r\n"
                       "Host: %s\r\n"
                       "Authorization: Basic %s\r\n"
                       "\r\n",
                       location_id, host, auth_b64);

    if (len < 0 || (size_t)len >= buffer_size) {
        debug_log_with_color(COLOR_RED, "[SEATSURFING] HTTP request too large\n");
        return -1;
    }

    return len;
}

// --- JSON parser ---

static seat_info_t seatsurfing_parse_seat_info(const char *json,
                                               const char *target_space_id_or_name) {
    seat_info_t info = {.is_available = true, .user_email = {0}, .desk_name = {0}};

    const char *target =
        (target_space_id_or_name && *target_space_id_or_name) ? target_space_id_or_name : NULL;

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return info;
    }

    if (cJSON_IsArray(root)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, root) {
            if (!cJSON_IsObject(item))
                continue;
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");

            bool match = false;
            if (!target) {
                // take the first entry as fallback
                match = true;
            } else {
                if ((cJSON_IsString(id) && strcmp(id->valuestring, target) == 0) ||
                    (cJSON_IsString(name) && strcmp(name->valuestring, target) == 0)) {
                    match = true;
                }
            }

            if (!match)
                continue;

            if (cJSON_IsString(name)) {
                strncpy(info.desk_name, name->valuestring, sizeof(info.desk_name) - 1);
            }

            const cJSON *available = cJSON_GetObjectItemCaseSensitive(item, "available");
            if (cJSON_IsBool(available)) {
                info.is_available = cJSON_IsTrue(available);
            }

            if (!info.is_available) {
                const cJSON *bookings = cJSON_GetObjectItemCaseSensitive(item, "bookings");
                if (cJSON_IsArray(bookings)) {
                    const cJSON *b = cJSON_GetArrayItem(bookings, 0);
                    if (cJSON_IsObject(b)) {
                        const cJSON *email = cJSON_GetObjectItemCaseSensitive(b, "userEmail");
                        if (cJSON_IsString(email)) {
                            strncpy(info.user_email, email->valuestring,
                                    sizeof(info.user_email) - 1);
                        }
                    }
                }
            }
            break; // found target
        }
    }

    cJSON_Delete(root);
    return info;
}

// --- use_case_t: SeatSurfing page catalog, input map, and export ---

static const page_def_t page_seatsurfing = {"seatsurfing", render_page_seatsurfing, true};

enum {
    SS_PAGE_SEATSURFING,
    SS_PAGE_DND,
    SS_PAGE_DECISION_MAKER,
    SS_PAGE_WIFI_SETUP,
    SS_PAGE_NFC_TEXT,
    SS_PAGE_COUNT
};

static const page_def_t *ss_pages[] = {
    [SS_PAGE_SEATSURFING] = &page_seatsurfing,
    [SS_PAGE_DND] = &page_dnd,
    [SS_PAGE_DECISION_MAKER] = &page_decision_maker,
    [SS_PAGE_WIFI_SETUP] = &page_wifi_setup,
    [SS_PAGE_NFC_TEXT] = &page_nfc_text,
    NULL,
};

static const input_map_entry_t ss_input_map[] = {
    {INPUT_BUTTON(0), SS_PAGE_SEATSURFING},
    {INPUT_BUTTON(1), SS_PAGE_DND},
    {INPUT_BUTTON(2), SS_PAGE_DECISION_MAKER},
    {INPUT_BUTTON(3), SS_PAGE_WIFI_SETUP},
    {INPUT_BUTTON(4), PAGE_ACTION_SETUP},
    {INPUT_BUTTON(5), SS_PAGE_DND},
    {INPUT_BUTTON(6), SS_PAGE_DECISION_MAKER},
    {INPUT_BUTTON(7), PAGE_ACTION_SETUP},
    // NFC opcodes (for future direct-source resolution)
    {INPUT_NFC(ST25_OPCODE_PAGE_0), SS_PAGE_SEATSURFING},
    {INPUT_NFC(ST25_OPCODE_REFRESH), SS_PAGE_SEATSURFING},
    {INPUT_NFC(ST25_OPCODE_PAGE_2), SS_PAGE_DECISION_MAKER},
    {INPUT_NFC(ST25_OPCODE_TEXT), SS_PAGE_NFC_TEXT},
    // NFC text mapped to pushbutton value (intermediate: NFC→pushbutton still in main.c)
    {INPUT_BUTTON(16), SS_PAGE_NFC_TEXT},
    {INPUT_MAP_END, 0},
};

// --- Lifecycle: run (fetch) and render ---

static WifiResult ss_last_wifi_result;

static bool seatsurfing_make_request(void) {
    char userpass[128];
    snprintf(userpass, sizeof(userpass), "%s:%s", seatsurfing_config_flash.data.username,
             seatsurfing_config_flash.data.password);

    char auth_b64[192];
    base64_encode(userpass, strlen(userpass), auth_b64, sizeof(auth_b64));

    char header[HTTP_REQUEST_MAX];
    int hlen =
        seatsurfing_build_http_request(header, sizeof(header), seatsurfing_config_flash.data.host,
                                       seatsurfing_config_flash.data.location_id, auth_b64);
    if (hlen < 0) {
        debug_log_with_color(COLOR_RED, "[SEATSURFING] Failed to build HTTP request\n");
        return false;
    }

    debug_log("Constructed HTTP Header:\n%s\n", header);

    ip_addr_t ip;
    IP4_ADDR(&ip, seatsurfing_config_flash.data.ip[0], seatsurfing_config_flash.data.ip[1],
             seatsurfing_config_flash.data.ip[2], seatsurfing_config_flash.data.ip[3]);

    http_sync_reset();
    http_result_t result = http_request_async(&ip, seatsurfing_config_flash.data.port, header,
                                              http_default_completion, NULL);
    if (result != HTTP_SUCCESS) {
        debug_log_with_color(COLOR_RED, "HTTP request failed to start: %d\n", result);
        return false;
    }
    return true;
}

static void *ss_run(float battery_voltage, float coin_cell_voltage) {
    set_data_callback(seatsurfing_data_received, NULL);
    ss_last_wifi_result =
        http_run_with_wifi(seatsurfing_make_request, battery_voltage, coin_cell_voltage);
    return (ss_last_wifi_result == WIFI_SUCCESS) ? seatsurfing_data : NULL;
}

static void ss_render(uint8_t *image, float battery_voltage, int page, const void *data) {
    if (!data && page >= 0 && ss_pages[page] && ss_pages[page]->needs_wifi) {
        if (ss_last_wifi_result == WIFI_ERROR_CONNECTION)
            render_page_error(image, "WiFi Error!", "Unable to connect to WiFi",
                              "Please check the WiFi settings.");
        else
            render_page_error(image, "Server Error!", "Unable to reach the server",
                              "Please check the server status.");
        return;
    }

    if (page >= 0 && ss_pages[page])
        ss_pages[page]->render(image, battery_voltage);
    else
        render_page_fallback(page, image, battery_voltage);
}

const use_case_t use_case = {
    .name = "seatsurfing",
    .pages = ss_pages,
    .input_map = ss_input_map,
    .default_page = SS_PAGE_SEATSURFING,
    .run = ss_run,
    .render = ss_render,
    .free_data = NULL,
};
