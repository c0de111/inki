#include "seatsurfing/client.h"
#include "base64.h"
#define LOG_MODULE LOG_MOD_SEATSURFING
#include "boot_input.h"
#include "debug.h"
#include "epaper_pages_shared.h"
#include "flash.h"
#include "http_client.h"
#include "lwip/ip_addr.h"
#include "pico/stdlib.h"
#include "rtc.h"
#include "seatsurfing/epaper_pages.h"
#include "st25_io.h"
#include "third_party/cjson/cJSON.h"
#include "tls_trust_store.h"
#include "use_case.h"
#include "wifi.h"
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

    dlog("[SEATSURFING] Parsed %u seats (first avail=%s, user=%s)\n", seat_count,
         seatsurfing_data[0].is_available ? "YES" : "NO",
         seatsurfing_data[0].user_email[0] ? seatsurfing_data[0].user_email : "None");
}

// --- HTTP request builder ---

static int seatsurfing_build_http_request(char *buffer, size_t buffer_size, const char *host,
                                          const char *location_id, const char *auth_b64,
                                          const char *bearer_token, const char *enter_iso,
                                          const char *leave_iso) {
    if (!buffer || !host || !location_id)
        return -1;

    int len;
    if (bearer_token) {
        len = snprintf(buffer, buffer_size,
                       "GET /location/%s/space/availability?enter=%s&leave=%s HTTP/1.0\r\n"
                       "Host: %s\r\n"
                       "Authorization: Bearer %s\r\n"
                       "\r\n",
                       location_id, enter_iso, leave_iso, host, bearer_token);
    } else {
        if (!auth_b64)
            return -1;
        len = snprintf(buffer, buffer_size,
                       "GET /location/%s/space/availability HTTP/1.0\r\n"
                       "Host: %s\r\n"
                       "Authorization: Basic %s\r\n"
                       "\r\n",
                       location_id, host, auth_b64);
    }

    if (len < 0 || (size_t)len >= buffer_size) {
        dlog("[SEATSURFING] HTTP request too large\n");
        return -1;
    }

    return len;
}

// --- JSON parser ---

static seat_info_t seatsurfing_parse_seat_info(const char *json,
                                               const char *target_space_id_or_name) {
    seat_info_t info = {.is_available = true, .user_email = {0}, .desk_name = {0}, .space_id = {0}};

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

            if (cJSON_IsString(id)) {
                strncpy(info.space_id, id->valuestring, sizeof(info.space_id) - 1);
            }
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

// --- Native SeatSurfing / NFC booking state ---

static char s_org_id[37] = {0}; // discovered via /organization/domain/{host}
static char s_access_token[1024] = {0};
static char s_booking_response[128] = {0}; // first 127 bytes of booking server reply

static void ss_login_response_received(const char *data, size_t length, void *arg) {
    (void)arg;
    s_access_token[0] = '\0';
    if (!data || length == 0) {
        debug_status("ERROR", "Booking: login response empty\n");
        return;
    }
    cJSON *root = cJSON_Parse(data);
    if (!root) {
        debug_status("ERROR", "Booking: login JSON parse failed\n");
        return;
    }
    const cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "accessToken");
    if (cJSON_IsString(token) && token->valuestring) {
        strncpy(s_access_token, token->valuestring, sizeof(s_access_token) - 1);
        debug_status("OK", "Booking: login OK (token %d bytes)\n", (int)strlen(s_access_token));
    } else {
        // Log the raw response so we can see the server error
        char snippet[64] = {0};
        strncpy(snippet, data, sizeof(snippet) - 1);
        debug_status("ERROR", "Booking: login no token — resp: %.63s\n", snippet);
    }
    cJSON_Delete(root);
}

static void ss_booking_response_received(const char *data, size_t length, void *arg) {
    (void)arg;
    s_booking_response[0] = '\0';
    if (!data || length == 0)
        return;
    size_t copy_len =
        length < sizeof(s_booking_response) - 1 ? length : sizeof(s_booking_response) - 1;
    memcpy(s_booking_response, data, copy_len);
    s_booking_response[copy_len] = '\0';
}

static void ss_org_response_received(const char *data, size_t length, void *arg) {
    (void)arg;
    s_org_id[0] = '\0';
    if (!data || length == 0)
        return;
    cJSON *root = cJSON_Parse(data);
    if (!root)
        return;
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsString(id) && id->valuestring)
        strncpy(s_org_id, id->valuestring, sizeof(s_org_id) - 1);
    cJSON_Delete(root);
}

static int ss_build_login_request(char *buf, size_t buf_size, const char *host, const char *email,
                                  const char *password, const char *org_id) {
    char body[256];
    int body_len = snprintf(body, sizeof(body),
                            "{\"email\":\"%s\",\"password\":\"%s\",\"organizationId\":\"%s\"}",
                            email, password, org_id);
    if (body_len < 0 || (size_t)body_len >= sizeof(body))
        return -1;

    int len = snprintf(buf, buf_size,
                       "POST /auth/login HTTP/1.0\r\n"
                       "Host: %s\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: %d\r\n"
                       "\r\n"
                       "%s",
                       host, body_len, body);
    if (len < 0 || (size_t)len >= buf_size)
        return -1;
    return len;
}

static int ss_build_booking_request(char *buf, size_t buf_size, const char *host,
                                    const char *space_id, const char *enter_iso,
                                    const char *leave_iso, const char *user_email) {
    char body[384];
    int body_len = snprintf(body, sizeof(body),
                            "{\"spaceId\":\"%s\",\"enter\":\"%s\","
                            "\"leave\":\"%s\",\"userEmail\":\"%s\"}",
                            space_id, enter_iso, leave_iso, user_email);
    if (body_len < 0 || (size_t)body_len >= sizeof(body))
        return -1;

    int len = snprintf(buf, buf_size,
                       "POST /booking/ HTTP/1.0\r\n"
                       "Host: %s\r\n"
                       "Authorization: Bearer %s\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: %d\r\n"
                       "\r\n"
                       "%s",
                       host, s_access_token, body_len, body);
    if (len < 0 || (size_t)len >= buf_size)
        return -1;
    return len;
}

// DNS mode steps 0-2: org discovery → JWT login → availability fetch (Bearer + end-of-day).
// Populates s_org_id, s_access_token, seatsurfing_data[]. Used by both display and booking paths.
static bool ss_dns_login_and_fetch(const ip_addr_t *ip, uint16_t port, const char *host,
                                   const char *email, const char *password, uint32_t timeout) {
    char req[HTTP_REQUEST_MAX];
    int rlen;

    // Step 0: org discovery
    rlen = snprintf(req, sizeof(req),
                    "GET /organization/domain/%s HTTP/1.0\r\n"
                    "Host: %s\r\n"
                    "\r\n",
                    host, host);
    if (rlen < 0 || (size_t)rlen >= sizeof(req))
        return false;
    s_org_id[0] = '\0';
    set_data_callback(ss_org_response_received, NULL);
    if (!http_request_sync(ip, port, req, timeout) || !s_org_id[0]) {
        debug_status("ERROR", "SeatSurfing: org discovery failed\n");
        return false;
    }
    dlog("SeatSurfing: org_id=%.8s...\n", s_org_id);

    // Step 1: login → JWT
    s_access_token[0] = '\0';
    rlen = ss_build_login_request(req, sizeof(req), host, email, password, s_org_id);
    if (rlen < 0) {
        debug_status("ERROR", "SeatSurfing: login request build failed\n");
        return false;
    }
    set_data_callback(ss_login_response_received, NULL);
    if (!http_request_sync(ip, port, req, timeout) || !s_access_token[0]) {
        debug_status("ERROR", "SeatSurfing: login failed\n");
        return false;
    }

    // Step 2: availability with Bearer + end-of-day window
    rtc_time_t now;
    if (rtc_read_time(&now) != 0)
        return false;
    char enter_iso[21], leave_iso[21];
    rtc_to_iso8601(&now, enter_iso, sizeof(enter_iso));
    rtc_time_t eod = now;
    eod.hours = 23;
    eod.minutes = 59;
    eod.seconds = 59;
    rtc_to_iso8601(&eod, leave_iso, sizeof(leave_iso));
    rlen = seatsurfing_build_http_request(req, sizeof(req), host,
                                          seatsurfing_config_flash.data.location_id, NULL,
                                          s_access_token, enter_iso, leave_iso);
    if (rlen < 0) {
        debug_status("ERROR", "SeatSurfing: availability request build failed\n");
        return false;
    }
    set_data_callback(seatsurfing_data_received, NULL);
    return http_request_sync(ip, port, req, timeout);
}

// Execute the full booking session: login → fetch availability → POST booking.
// Called from seatsurfing_make_request() when nfc_booking_email is set.
// Booking failures are non-fatal: availability data is shown regardless.
static bool ss_do_booking(const ip_addr_t *ip, bool use_dns) {
    char req[HTTP_REQUEST_MAX];
    int rlen;
    const char *host = seatsurfing_config_flash.data.host;
    uint16_t port = seatsurfing_config_flash.data.port;
    uint32_t timeout = device_config_flash.data.max_wait_data_wifi * 50;

    debug_status("INFO", "Booking: starting for user=%s\n", nfc_booking_email);

    if (use_dns) {
        if (!ss_dns_login_and_fetch(ip, port, host, seatsurfing_config_flash.data.booking_email,
                                    seatsurfing_config_flash.data.booking_password, timeout))
            return false;
    } else {
        // Direct-IP mode: login with username-prefix org_id + Basic Auth availability
        char booking_org_id[37] = {0};
        strncpy(booking_org_id, seatsurfing_config_flash.data.username, 36);
        rlen = ss_build_login_request(
            req, sizeof(req), host, seatsurfing_config_flash.data.booking_email,
            seatsurfing_config_flash.data.booking_password, booking_org_id);
        if (rlen < 0) {
            debug_status("ERROR", "Booking: login request build failed\n");
            return false;
        }
        set_data_callback(ss_login_response_received, NULL);
        if (!http_request_sync(ip, port, req, timeout) || !s_access_token[0]) {
            debug_status("ERROR", "Booking: login failed\n");
            return false;
        }

        char userpass[128];
        snprintf(userpass, sizeof(userpass), "%s:%s", seatsurfing_config_flash.data.username,
                 seatsurfing_config_flash.data.password);
        char auth_b64[192];
        base64_encode(userpass, strlen(userpass), auth_b64, sizeof(auth_b64));
        rlen = seatsurfing_build_http_request(req, sizeof(req), host,
                                              seatsurfing_config_flash.data.location_id, auth_b64,
                                              NULL, NULL, NULL);
        if (rlen < 0) {
            debug_status("ERROR", "Booking: availability request build failed\n");
            return false;
        }
        set_data_callback(seatsurfing_data_received, NULL);
        if (!http_request_sync(ip, port, req, timeout)) {
            debug_status("ERROR", "Booking: availability fetch failed\n");
            return false;
        }
    }

    // Find first available configured seat and remember its index for optimistic update
    char free_space_id[64] = {0};
    int8_t free_seat_idx = -1;
    uint8_t seat_count = seatsurfing_config_flash.data.seat_count;
    if (seat_count == 0)
        seat_count = 1;
    if (seat_count > SEATSURFING_MAX_SEATS)
        seat_count = SEATSURFING_MAX_SEATS;
    for (uint8_t i = 0; i < seat_count; i++) {
        debug_status("INFO", "Booking: seat[%u] avail=%d id=%.20s\n", i,
                     seatsurfing_data[i].is_available, seatsurfing_data[i].space_id);
        if (free_seat_idx < 0 && seatsurfing_data[i].is_available &&
            seatsurfing_data[i].space_id[0]) {
            strncpy(free_space_id, seatsurfing_data[i].space_id, sizeof(free_space_id) - 1);
            free_seat_idx = (int8_t)i;
        }
    }
    if (!free_space_id[0]) {
        debug_status("WARN", "Booking: no free space found\n");
        return true;
    }
    debug_status("INFO", "Booking: using space_id=%.36s (idx=%d)\n", free_space_id, free_seat_idx);

    // Build enter/leave ISO 8601 timestamps from RTC
    rtc_time_t now;
    if (rtc_read_time(&now) != 0) {
        debug_status("ERROR", "Booking: RTC read failed\n");
        return true;
    }
    char enter_iso[21], leave_iso[21];
    rtc_to_iso8601(&now, enter_iso, sizeof(enter_iso));

    uint32_t enter_secs = now.hours * 3600u + now.minutes * 60u + now.seconds;
    uint32_t leave_secs = enter_secs + SEATSURFING_BOOKING_DURATION_S;
    rtc_time_t leave = now;
    if (leave_secs >= 24u * 3600u) {
        leave.hours = 23;
        leave.minutes = 59;
        leave.seconds = 59;
    } else {
        leave.hours = (uint8_t)(leave_secs / 3600u);
        leave.minutes = (uint8_t)((leave_secs % 3600u) / 60u);
        leave.seconds = (uint8_t)(leave_secs % 60u);
    }
    rtc_to_iso8601(&leave, leave_iso, sizeof(leave_iso));
    debug_status("INFO", "Booking: enter=%s leave=%s\n", enter_iso, leave_iso);

    // Step 3: POST booking
    rlen = ss_build_booking_request(req, sizeof(req), seatsurfing_config_flash.data.host,
                                    free_space_id, enter_iso, leave_iso, nfc_booking_email);
    if (rlen < 0) {
        debug_status("ERROR", "Booking: request build failed (too long?)\n");
        return true;
    }
    set_data_callback(ss_booking_response_received, NULL);
    bool transport_ok = http_request_sync(ip, seatsurfing_config_flash.data.port, req,
                                          device_config_flash.data.max_wait_data_wifi * 50);
    debug_status(transport_ok ? "OK" : "ERROR", "Booking: resp=%s\n",
                 s_booking_response[0] ? s_booking_response : "(empty)");

    // Optimistic display update: if transport succeeded and server didn't return an error body,
    // mark the seat as occupied so the display reflects the booking immediately.
    bool booking_ok = transport_ok && (s_booking_response[0] == '\0' ||
                                       strstr(s_booking_response, "\"message\"") == NULL);
    if (booking_ok && free_seat_idx >= 0) {
        seatsurfing_data[free_seat_idx].is_available = false;
        strncpy(seatsurfing_data[free_seat_idx].user_email, nfc_booking_email,
                sizeof(seatsurfing_data[free_seat_idx].user_email) - 1);
        debug_status("OK", "Booking: confirmed %s → seat[%d]\n", nfc_booking_email, free_seat_idx);
    }

    return true;
}

// --- use_case_t: SeatSurfing page catalog, input map, and export ---

static const page_def_t page_seatsurfing = {"seatsurfing", render_page_seatsurfing, true};

enum {
    SS_PAGE_SEATSURFING,
    SS_PAGE_DND,
    SS_PAGE_DECISION_MAKER,
    SS_PAGE_WIFI_SETUP,
    SS_PAGE_NFC_TEXT,
    SS_PAGE_NFC_IMAGE,
    SS_PAGE_COUNT
};

static const page_def_t *ss_pages[] = {
    [SS_PAGE_SEATSURFING] = &page_seatsurfing,
    [SS_PAGE_DND] = &page_dnd,
    [SS_PAGE_DECISION_MAKER] = &page_decision_maker,
    [SS_PAGE_WIFI_SETUP] = &page_wifi_setup,
    [SS_PAGE_NFC_TEXT] = &page_nfc_text,
    [SS_PAGE_NFC_IMAGE] = &page_nfc_image,
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
    {INPUT_NFC(ST25_OPCODE_DRAW_IMAGE), SS_PAGE_NFC_IMAGE},
    {INPUT_NFC(ST25_OPCODE_BOOK_SEAT), SS_PAGE_SEATSURFING},
    // NFC text mapped to pushbutton value (intermediate: NFC→pushbutton still in main.c)
    {INPUT_BUTTON(16), SS_PAGE_NFC_TEXT},
    {INPUT_MAP_END, 0},
};

// --- Lifecycle: run (fetch) and render ---

static bool seatsurfing_make_request(void) {
    ip_addr_t ip;
    bool use_dns =
        (seatsurfing_config_flash.data.ip[0] == 0 && seatsurfing_config_flash.data.ip[1] == 0 &&
         seatsurfing_config_flash.data.ip[2] == 0 && seatsurfing_config_flash.data.ip[3] == 0);
    if (use_dns) {
        debug_status("INFO", "SeatSurfing: resolving %s via DNS\n",
                     seatsurfing_config_flash.data.host);
        if (!wifi_resolve_hostname(seatsurfing_config_flash.data.host, &ip, 5000)) {
            debug_status("ERROR", "SeatSurfing: DNS resolution failed for %s\n",
                         seatsurfing_config_flash.data.host);
            return false;
        }
    } else {
        IP4_ADDR(&ip, seatsurfing_config_flash.data.ip[0], seatsurfing_config_flash.data.ip[1],
                 seatsurfing_config_flash.data.ip[2], seatsurfing_config_flash.data.ip[3]);
    }

    if (nfc_booking_email[0]) {
        return ss_do_booking(&ip, use_dns);
    }

    const char *host = seatsurfing_config_flash.data.host;
    const char *location_id = seatsurfing_config_flash.data.location_id;
    uint16_t port = seatsurfing_config_flash.data.port;
    uint32_t timeout = device_config_flash.data.max_wait_data_wifi * 50;
    char req[HTTP_REQUEST_MAX];

    if (use_dns) {
        return ss_dns_login_and_fetch(&ip, port, host, seatsurfing_config_flash.data.username,
                                      seatsurfing_config_flash.data.password, timeout);
    }

    // LAN relay flow: single GET with Basic Auth
    char userpass[128];
    snprintf(userpass, sizeof(userpass), "%s:%s", seatsurfing_config_flash.data.username,
             seatsurfing_config_flash.data.password);

    char auth_b64[192];
    base64_encode(userpass, strlen(userpass), auth_b64, sizeof(auth_b64));

    int hlen = seatsurfing_build_http_request(req, sizeof(req), host, location_id, auth_b64, NULL,
                                              NULL, NULL);
    if (hlen < 0) {
        dlog("[SEATSURFING] Failed to build HTTP request\n");
        return false;
    }

    dlog("Constructed HTTP Header:\n%s\n", req);

    return http_request_sync(&ip, port, req, timeout);
}

static void *ss_run(void) {
    tls_create_config_after_wifi();
    set_data_callback(seatsurfing_data_received, NULL);
    bool ok = seatsurfing_make_request();
    return ok ? seatsurfing_data : NULL;
}

static void ss_render(uint8_t *image, float battery_voltage, int page, const void *data) {
    if (page == PAGE_WIFI_ERROR) {
        render_page_error(image, "WiFi Error!", "Unable to connect to WiFi",
                          "Please check the WiFi settings.");
        return;
    }
    if (!data && page >= 0 && ss_pages[page] && ss_pages[page]->needs_wifi) {
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
