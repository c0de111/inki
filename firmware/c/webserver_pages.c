/*
 * ==============================================================================
 * webserver_pages.c - HTML Page Generation & Form Processing for inki Webserver
 * ==============================================================================
 *
 * Contains all HTML page generation functions and form processing handlers
 * for webserver.c. This module handles:
 *
 * - HTML page generation for the web configuration interface
 * - Form data processing and validation
 * - Configuration saving to flash memory
 * - User feedback and error handling
 *
 */

#include "webserver_pages.h"
#include "GUI_Paint.h"
#include "config.h"
#include "debug.h"
#include "ds3231.h"
#include "flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "i2c_probe.h"
#include "lwip/tcp.h"
#include "main.h"
#include "morse.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/time.h"
#include "version.h"
#include "webserver.h"
#include "webserver_utils.h"
#include "wifi.h"
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// HTML PAGE GENERATION FUNCTIONS
// =============================================================================

static const char *fw_use_case_name_from_id(uint8_t use_case_id) {
    switch (use_case_id) {
    case USE_CASE_ID_SEATSURFING:
        return "SeatSurfing";
    case USE_CASE_ID_HISTORIAN:
        return "Historian";
    case USE_CASE_ID_HOMEMATIC:
        return "Homematic";
    case USE_CASE_ID_WEATHERMAP:
        return "Weathermap";
    case USE_CASE_ID_NEW_USECASE:
        return "NewUseCase";
    default:
        return NULL;
    }
}

static bool fw_use_case_meta_valid(const firmware_header_t *header) {
    if (!header || header->meta_version != 1) {
        return false;
    }

    const char *expected_name = fw_use_case_name_from_id(header->use_case_id);
    if (!expected_name) {
        return false;
    }

    size_t name_len = 0;
    while (name_len < sizeof(header->use_case_name) && header->use_case_name[name_len] != '\0') {
        name_len++;
    }
    if (name_len == 0 || name_len == sizeof(header->use_case_name)) {
        return false;
    }

    return strcmp(header->use_case_name, expected_name) == 0;
}

static void format_slot_use_case(uint8_t slot, char *out, size_t out_len, bool with_id) {
    if (!out || out_len == 0) {
        return;
    }

    uint32_t offset = (slot == 0)   ? FIRMWARE_SLOT0_FLASH_OFFSET
                      : (slot == 1) ? FIRMWARE_SLOT1_FLASH_OFFSET
                                    : 0;

    if (offset == 0) {
        snprintf(out, out_len, "n/a");
        return;
    }

    const firmware_header_t *header = (const firmware_header_t *)FLASH_PTR(offset);
    if (memcmp(header->magic, FIRMWARE_MAGIC, FIRMWARE_MAGIC_LEN) != 0 || header->valid_flag != 1) {
        snprintf(out, out_len, "empty");
        return;
    }

    if (fw_use_case_meta_valid(header)) {
        if (with_id) {
            snprintf(out, out_len, "%s (%u)", header->use_case_name, (unsigned)header->use_case_id);
        } else {
            snprintf(out, out_len, "%s", header->use_case_name);
        }
    } else if (header->meta_version == 0 && header->use_case_id == 0 &&
               header->use_case_name[0] == '\0') {
        snprintf(out, out_len, "legacy/unknown");
    } else {
        snprintf(out, out_len, "invalid metadata");
    }
}

/**
 * @brief Generates and sends the main landing page with navigation menu
 * @param tpcb TCP connection pointer
 *
 * Creates the main setup page with links to all configuration sections.
 * Includes auto-refresh and timeout countdown display.
 */
void send_landing_page(struct tcp_pcb *tpcb) {
    char page[4096];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta charset=\"UTF-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
             "<meta http-equiv=\"refresh\" content=\"5\">"
             "<title>inki Setup</title>"
             "<style>"
             "body { font-family: sans-serif; text-align: center; }"
             "a { display: inline-block; padding: 0.6em 1em; font-size: 1em; margin: 0.5em; width: "
             "80%%; max-width: 200px; background: #eee; border: 1px solid #ccc; border-radius: "
             "5px; text-decoration: none; color: black; }"
             "a:hover { background: #ddd; }"
             "p { margin-top: 2em; font-size: 0.9em; }"
             "</style></head><body>\n");

    strcat(page,
           "<h1>inki Setup</h1>"
           "<a href=\"/device_status\">Device Status</a><br>"
           "<a href=\"/settings_transfer\">Import/Export Settings</a><br>"
#ifdef USE_CASE_SEATSURFING
           "<a href=\"/seatsurfing\">Seatsurfing Settings</a><br>"
#elif defined(USE_CASE_HISTORIAN)
           "<a href=\"/historian\">Historian Settings</a><br>"
#elif defined(USE_CASE_HOMEMATIC)
           "<a href=\"/homematic\">Homematic Settings</a><br>"
#elif defined(USE_CASE_WEATHERMAP)
           "<a href=\"/weathermap\">Weathermap</a><br>"
#endif
           "<a href=\"/device_settings\">Device Settings</a><br>"
           // Hidden from landing page by request; feature remains available via /message route.
           // "<a href=\"/message\">Your Custom Message</a><br>"
           "<a href=\"/firmware_update\">Firmware Update</a><br>"
           "<a href=\"/shutdown\">Reboot</a>");
    /*
        strcat(page,
               "<img src=\"/logo\" alt=\"inki logo\" width=\"104\" height=\"95\"><br>");*/

    snprintf(page + strlen(page), sizeof(page) - strlen(page), "<p>%s</p></body></html>",
             timeout_info);
    debug_log("Landing page length: %d\n", strlen(page));

    send_response(tpcb, page);
}

#ifdef USE_CASE_WEATHERMAP
void send_weathermap_page(struct tcp_pcb *tpcb, const char *message) {
    char page[4096];
    const char *info = (message && *message) ? message : "";

    weathermap_config_t cfg;
    if (!load_weathermap_config(&cfg)) {
        init_weathermap_config(&cfg);
    }

    double lat = cfg.data.center_lat;
    double lon = cfg.data.center_lon;
    double half_m = cfg.data.half_width_m;

    if (!isfinite(lat) || fabs(lat) > 90.0)
        lat = WEATHERMAP_DEFAULT_CENTER_LAT;
    if (!isfinite(lon) || fabs(lon) > 180.0)
        lon = WEATHERMAP_DEFAULT_CENTER_LON;
    if (!isfinite(half_m) || half_m <= 0.0)
        half_m = WEATHERMAP_DEFAULT_HALF_WIDTH_M;

    double half_km = half_m / 1000.0;
    double span_km = half_km * 2.0;

    char lat_buf[32];
    char lon_buf[32];
    char half_buf[32];

    struct lconv *lc = localeconv();
    char decimal = lc && lc->decimal_point && lc->decimal_point[0] ? lc->decimal_point[0] : '.';

    snprintf(lat_buf, sizeof(lat_buf), "%.6f", lat);
    snprintf(lon_buf, sizeof(lon_buf), "%.6f", lon);
    snprintf(half_buf, sizeof(half_buf), "%.2f", half_km);

    if (decimal != '.') {
        for (char *p = lat_buf; *p; ++p)
            if (*p == decimal)
                *p = '.';
        for (char *p = lon_buf; *p; ++p)
            if (*p == decimal)
                *p = '.';
        for (char *p = half_buf; *p; ++p)
            if (*p == decimal)
                *p = '.';
    }

    snprintf(
        page, sizeof(page),
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>Weathermap</title>"
        "<style>body{font-family:sans-serif;max-width:720px;margin:auto;padding:1em;}"
        "form{background:#f7f7f7;padding:1em;border:1px solid "
        "#ccc;border-radius:6px;margin-bottom:1.2em;max-width:24em;}"
        "label{display:block;margin-top:.6em;font-weight:bold;}"
        "input[type=text]{width:14em;max-width:100%%;padding:.5em;font-size:1em;margin-top:.2em;}"
        "button{margin-top:1em;padding:.6em 1em;font-size:1em;border:1px solid "
        "#444;border-radius:6px;background:#fff;}"
        "a.btn{display:inline-block;padding:.6em 1em;background:#eee;border:1px solid "
        "#ccc;border-radius:6px;text-decoration:none;color:#000;margin:.4em 0;}"
        "p.note{color:#555;font-size:.9em;max-width:24em;}"
        "p.flash-ok{color:green;}"
        "p.flash-error{color:#b00;}"
        "</style></head><body>\n"
        "<h2>Weathermap</h2>");
    if (*info) {
        const char *flash_cls = (strncmp(info, "⚠", 3) == 0) ? "flash-error" : "flash-ok";
        snprintf(page + strlen(page), sizeof(page) - strlen(page), "<p class='%s'>%s</p>",
                 flash_cls, info);
    }

    snprintf(page + strlen(page), sizeof(page) - strlen(page),
             "<form method=\"POST\" action=\"/weathermap\">"
             "<label for=lat>Center latitude (°)</label>"
             "<input id=lat type=\"text\" inputmode=\"decimal\" pattern=\"-?[0-9]+(\\.[0-9]+)?\" "
             "name=\"text1\" value=\"%s\" required>"
             "<label for=lon>Center longitude (°)</label>"
             "<input id=lon type=\"text\" inputmode=\"decimal\" pattern=\"-?[0-9]+(\\.[0-9]+)?\" "
             "name=\"text2\" value=\"%s\" required>"
             "<label for=span>Half width (km)</label>"
             "<input id=span type=\"text\" inputmode=\"decimal\" pattern=\"[0-9]+(\\.[0-9]+)?\" "
             "name=\"text3\" value=\"%s\" required>"
             "<p class=\"note\">Span on ground: %.1f km (width = 2 × half width).<br>Use '.' as "
             "decimal separator.</p>"
             "<p class=\"note\">Saving clears the cached PNG; the device refetches the map on the "
             "next weather update.</p>"
             "<button type=\"submit\">Save settings</button>"
             "</form>",
             lat_buf, lon_buf, half_buf, span_km);
    // Check if a cached PNG exists in flash
    uint32_t staged_bytes = 0;
    bool has_png = get_weathermap_meta(&staged_bytes) && staged_bytes >= 8;
    if (has_png) {
        // Optional quick signature check
        const uint8_t *data = FLASH_PTR(FIRMWARE_SLOT1_FLASH_OFFSET);
        if (!(data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')) {
            has_png = false;
        }
    }

    uint16_t cached_w = 0, cached_h = 0;
    uint32_t cached_len = 0;
    bool has_2bpp = weathermap_flash_info(&cached_w, &cached_h, &cached_len);

    if (has_png) {
        snprintf(page + strlen(page), sizeof(page) - strlen(page),
                 "<p class=\"note\">Cached PNG in slot1: %u bytes.</p>\n", staged_bytes);
        strcat(page, "<p><img src=\"/weathermap.png\" alt=\"cached map\" "
                     "style=\"max-width:100%;height:auto;border:1px solid #ccc\"></p>\n");
        strcat(page, "<p><a class=\"btn\" href=\"/weathermap_clear\">Clear cached image</a></p>\n");
    } else {
        strcat(page, "<p><i>No cached map stored in device.</i></p>\n");
    }

    if (has_2bpp) {
        snprintf(page + strlen(page), sizeof(page) - strlen(page),
                 "<p class=\"note\">Rendered 2-bit image: %ux%u, %u bytes.</p>\n", cached_w,
                 cached_h, cached_len);
    } else {
        strcat(page, "<p class=\"note\"><i>No compressed 2-bit image stored.</i></p>\n");
    }

    strcat(page, "<p><a href=\"/\">Back</a></p></body></html>");
    send_response(tpcb, page);
}
#endif

void send_message_page(struct tcp_pcb *tpcb, const char *message) {
    char page[4096];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    const char *info = (message && *message) ? message : "";

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta charset=\"UTF-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
             "<title>Your Custom Message</title>"
             "<style>body{font-family:sans-serif;max-width:640px;margin:auto;padding:1em;}"
             "input,textarea,button{font-size:1em;width:100%%;padding:.6em;margin:.4em 0;}"
             "label{font-weight:bold;display:block;margin-top:.6em;}"
             "></style></head><body>\n"
             "<h2>Your Custom Message</h2>\n");

    if (*info) {
        snprintf(page + strlen(page), sizeof(page) - strlen(page), "<p style='color:green'>%s</p>",
                 info);
    }

    bool can_print = (device_config_flash.data.epapertype != EPAPER_NONE);
    snprintf(page + strlen(page), sizeof(page) - strlen(page),
             "<form method=\"POST\" action=\"/message\">"
             "<label for=msg>Message</label>"
             "<textarea id=msg name=\"text1\" rows=4 maxlength=240 placeholder=\"Type your "
             "message\"></textarea>"
             "<fieldset><legend>Action</legend>"
             "<label><input type=\"radio\" name=\"action\" value=\"morse\" checked> Morse on "
             "LED</label><br>"
             "<div style=\"padding-left:1em\">"
             "<label for=unit>Unit (ms)</label>"
             "<input id=unit name=\"unit_ms\" type=\"number\" min=\"50\" max=\"2000\" step=\"10\" "
             "value=\"%d\">"
             "</div>"
             "<label><input type=\"radio\" name=\"action\" value=\"print\" %s> Print on ePaper & "
             "Power Down</label>"
             "<div style=\"padding-left:1em\">"
             "<label for=font>Font size</label>"
             "<select id=font name=\"font_size\"><option>small</option><option "
             "selected>medium</option><option>large</option></select>"
             "<label for=align>Alignment</label>"
             "<select id=align name=\"align\"><option>left</option><option "
             "selected>center</option></select>"
             "</div>"
             "</fieldset>"
             "<button type=submit>Apply</button>"
             "</form>"
             "<form method=\"POST\" action=\"/message\" style=\"margin-top:1em\">"
             "<input type=\"hidden\" name=\"abort\" value=\"1\">"
             "<input type=\"hidden\" name=\"action\" value=\"morse\">"
             "<button type=\"submit\">Stop Morse</button>"
             "</form>"
             "<p><a href=\"/\">Back to Start</a></p>",
             LED_MORSE_UNIT_MS, can_print ? "" : "disabled");

    snprintf(page + strlen(page), sizeof(page) - strlen(page), "<p>%s</p></body></html>",
             timeout_info);

    send_response(tpcb, page);
}

static const sFONT *pick_font(const char *size) {
    if (!size)
        return &font_ubuntu_mono_14pt;
    if (strncmp(size, "large", 5) == 0)
        return &font_ubuntu_mono_18pt_bold;
    if (strncmp(size, "small", 5) == 0)
        return &font_ubuntu_mono_10pt;
    return &font_ubuntu_mono_14pt;
}

static void draw_wrapped_text(UBYTE *img, int width, int height, const char *msg, const sFONT *font,
                              bool center) {
    Paint_SelectImage(img);
    int margin = 20;
    int x0 = margin;
    int y = margin;
    int max_cols = (width - 2 * margin) / font->Width;
    int line_h = font->Height + 2;
    if (max_cols < 1)
        return;

    const char *p = msg;
    char line[256];
    while (*p && y + font->Height <= height - margin) {
        int count = 0;
        const char *line_start = p;
        const char *last_space = NULL;
        while (*p && count < max_cols) {
            if (*p == ' ')
                last_space = p;
            p++;
            count++;
        }
        if (*p && last_space && last_space > line_start) {
            // wrap at last space
            int take = last_space - line_start;
            if (take > (int)sizeof(line) - 1)
                take = sizeof(line) - 1;
            memcpy(line, line_start, take);
            line[take] = '\0';
            p = last_space + 1;
        } else {
            int take = p - line_start;
            if (take > (int)sizeof(line) - 1)
                take = sizeof(line) - 1;
            memcpy(line, line_start, take);
            line[take] = '\0';
        }
        int x = x0;
        if (center) {
            size_t line_px_sz = strlen(line) * (size_t)font->Width;
            int line_px = (line_px_sz > (size_t)INT_MAX) ? INT_MAX : (int)line_px_sz;
            x = (width - line_px) / 2;
            if (x < margin)
                x = margin;
        }
        Paint_DrawString_EN(x, y, line, (sFONT *)font, WHITE, BLACK);
        y += line_h;
    }
}

void handle_form_message(struct tcp_pcb *tpcb, const char *body, size_t len) {
    web_submission_t result;
    parse_form_fields(body, len, &result);

    const char *action = (*result.action) ? result.action : "morse";
    const char *msg = result.text[0][0] ? result.text[0] : "INKI";

    if (strncmp(action, "morse", 5) == 0) {
        if (result.aborted) {
            morse_set_enabled(false);
            send_message_page(tpcb, "Morse stopped");
            return;
        }
        int unit = result.unit_ms;
        if (unit >= 50 && unit <= 2000)
            morse_set_unit_ms(unit);
        morse_set_message(msg);
        morse_set_enabled(true);
        char feedback[256];
        snprintf(feedback, sizeof(feedback), "Morsing started: %s", msg);
        send_message_page(tpcb, feedback);
        return;
    }

    // Print path
    if (device_config_flash.data.epapertype == EPAPER_NONE) {
        send_message_page(tpcb, "No ePaper configured – cannot print.");
        return;
    }

    UBYTE *BlackImage = init_epaper();
    if (!BlackImage) {
        send_message_page(tpcb, "Failed to init ePaper");
        return;
    }

    // Canvas is already selected by init_epaper(); ensure clear
    Paint_Clear(WHITE);

    int width = Paint.Width;
    int height = Paint.Height;
    const sFONT *font = pick_font(result.font_size);
    bool center = (strncmp(result.align, "left", 4) != 0);
    draw_wrapped_text(BlackImage, width, height, msg, font, center);

    epaper_finalize_and_powerdown(BlackImage);
    morse_set_enabled(false);

    // Power down shortly so HTTP reply can reach browser
    webserver_set_shutdown_time(make_timeout_time_ms(2000));
    send_message_page(tpcb, "Printed to ePaper. Powering down...");
}

/**
 * @brief Generates and sends the device status page showing system information
 * @param tpcb TCP connection pointer
 *
 * Displays comprehensive device status including:
 * - Current configuration (room name, Wi-Fi settings)
 * - RTC time (raw and DST-adjusted)
 * - Voltage readings (VCC, backup battery)
 * - MAC address, flash logo info
 * - Active firmware slot information
 */
void send_device_status_page(struct tcp_pcb *tpcb) {
    char page[16384];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    char buffer[2048];
    char buffer2[256];
    uint8_t mac_buf[6];

    extern ds3231_t ds3231;
    ds3231_data_t now;

    if (cyw43_wifi_get_mac(&cyw43_state, 0, mac_buf) != 0) {
        memset(mac_buf, 0, sizeof(mac_buf));
    }

    ds3231_read_current_time(&ds3231, &now);
    float vcc = read_battery_voltage(device_config_flash.data.conversion_factor);
    float vbat = read_coin_cell_voltage(device_config_flash.data.conversion_factor);
    float temp_c = read_onchip_temperature_c();
    float rtc_temp_c = read_ds3231_temperature_c();

    i2c_probe_result_t i2c_probe = {0};
    i2c_probe_expected_devices(&i2c_probe);

    memory_info_t mem = {0};
    get_memory_info(&mem);

    const char *vcc_color = (vcc > 3.5f) ? "green" : (vcc > 3.0f ? "orange" : "red");
    const char *vbat_color = (vbat > 3.1f) ? "green" : (vbat > 2.9f ? "orange" : "red");

    const bool st25_present = i2c_probe.st25_user_present || i2c_probe.st25_system_present;

    const char *ds3231_color =
        (i2c_probe.ds3231_present && i2c_probe.ds3231_status_ok && i2c_probe.ds3231_temp_ok)
            ? "green"
            : (i2c_probe.ds3231_present ? "orange" : "red");
    const char *bmp581_color = (i2c_probe.bmp581_present && i2c_probe.bmp581_chip_id_ok)
                                   ? "green"
                                   : (i2c_probe.bmp581_present ? "orange" : "red");
    const char *rv3028_color = (i2c_probe.rv3028_present && i2c_probe.rv3028_id_ok)
                                   ? "green"
                                   : (i2c_probe.rv3028_present ? "orange" : "red");
    const char *st25_color =
        (st25_present && i2c_probe.st25_ic_ref_ok) ? "green" : (st25_present ? "orange" : "red");

    const char *ds3231_state = i2c_probe.ds3231_present ? "detected" : "not detected";
    const char *bmp581_state = i2c_probe.bmp581_present ? "detected" : "not detected";
    const char *rv3028_state = i2c_probe.rv3028_present ? "detected" : "not detected";
    const char *st25_state = st25_present ? "detected" : "not detected";
    const char *use_case_slug = "unknown";
    switch (USE_CASE_ID) {
    case USE_CASE_ID_SEATSURFING:
        use_case_slug = "seatsurfing";
        break;
    case USE_CASE_ID_HISTORIAN:
        use_case_slug = "historian";
        break;
    case USE_CASE_ID_HOMEMATIC:
        use_case_slug = "homematic";
        break;
    case USE_CASE_ID_WEATHERMAP:
        use_case_slug = "weathermap";
        break;
    case USE_CASE_ID_NEW_USECASE:
        use_case_slug = "newusecase";
        break;
    default:
        break;
    }
    char device_type_label[32];
    snprintf(device_type_label, sizeof(device_type_label), "inki-%s", use_case_slug);

    char bmp581_addr_str[8] = "0x47/46";
    char rtc_temp_str[16] = "n/a";

    if (i2c_probe.bmp581_addr != 0xFFu) {
        snprintf(bmp581_addr_str, sizeof(bmp581_addr_str), "0x%02X", i2c_probe.bmp581_addr);
    }

    if (isnan(rtc_temp_c)) {
        snprintf(rtc_temp_str, sizeof(rtc_temp_str), "n/a");
    } else {
        float qf = rtc_temp_c * 4.0f;
        int q = (int)((qf >= 0.0f) ? (qf + 0.5f) : (qf - 0.5f));
        int whole = q / 4;
        int rem = q % 4;
        if (rem < 0) {
            rem += 4;
            whole -= 1;
        }
        switch (rem) {
        case 0:
            snprintf(rtc_temp_str, sizeof(rtc_temp_str), "%d.0", whole);
            break;
        case 1:
            snprintf(rtc_temp_str, sizeof(rtc_temp_str), "%d.25", whole);
            break;
        case 2:
            snprintf(rtc_temp_str, sizeof(rtc_temp_str), "%d.5", whole);
            break;
        default:
            snprintf(rtc_temp_str, sizeof(rtc_temp_str), "%d.75", whole);
            break;
        }
    }

    format_rtc_time(&now, buffer2, sizeof(buffer2));

    int logo_width = 0;
    int logo_height = 0;
    int logo_size = 0;
    bool has_logo = get_flash_logo_info(&logo_width, &logo_height, &logo_size);
    char logo_info_str[96] = "not present";
    if (has_logo) {
        snprintf(logo_info_str, sizeof(logo_info_str), "%dx%d px, %d Bytes", logo_width,
                 logo_height, logo_size);
    }

    char build0[16] = {0};
    char version0[32] = {0};
    char build1[16] = {0};
    char version1[32] = {0};
    uint32_t size0 = 0;
    uint32_t size1 = 0;
    uint32_t crc0 = 0;
    uint32_t crc1 = 0;
    uint8_t slot_index0 = 0;
    uint8_t slot_index1 = 0;
    uint8_t valid0 = 0;
    uint8_t valid1 = 0;

    bool has0 = get_firmware_slot_info(0, build0, version0, &size0, &crc0, &slot_index0, &valid0);
    bool has1 = get_firmware_slot_info(1, build1, version1, &size1, &crc1, &slot_index1, &valid1);

    char slot0_row[512];
    char slot1_row[512];
    if (has0) {
        char slot0_use_case[64] = "n/a";
        format_slot_use_case(0, slot0_use_case, sizeof(slot0_use_case), false);
        snprintf(slot0_row, sizeof(slot0_row),
                 "<tr><td>Slot 0</td><td class='mono'>%s</td><td "
                 "class='mono'>%s</td><td>%s</td><td class='mono'>%u</td><td>%u</td></tr>",
                 version0, slot0_use_case, build0, size0, valid0);
    } else {
        snprintf(slot0_row, sizeof(slot0_row),
                 "<tr><td>Slot 0</td><td colspan='5'><span class='red value'>empty or "
                 "invalid</span></td></tr>");
    }

    if (has1) {
        char slot1_use_case[64] = "n/a";
        format_slot_use_case(1, slot1_use_case, sizeof(slot1_use_case), false);
        snprintf(slot1_row, sizeof(slot1_row),
                 "<tr><td>Slot 1</td><td class='mono'>%s</td><td "
                 "class='mono'>%s</td><td>%s</td><td class='mono'>%u</td><td>%u</td></tr>",
                 version1, slot1_use_case, build1, size1, valid1);
    } else {
        snprintf(slot1_row, sizeof(slot1_row),
                 "<tr><td>Slot 1</td><td colspan='5'><span class='red value'>empty or "
                 "invalid</span></td></tr>");
    }

    snprintf(page, sizeof(page), "%s",
             "<!DOCTYPE html><html><head>"
             "<meta charset=\"UTF-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
             "<meta http-equiv=\"refresh\" content=\"300\">"
             "<title>Device Status</title>"
             "<style>"
             ":root { color-scheme: light; }"
             "* { box-sizing: border-box; }"
             "body { margin: 0; padding: 0.8rem; font-family: -apple-system, BlinkMacSystemFont, "
             "\"Segoe UI\", Roboto, Helvetica, Arial, sans-serif; background: #f5f7fa; color: "
             "#1f2937; line-height: 1.45; text-align: center; }"
             ".container { max-width: 800px; margin: 0 auto; }"
             "h1 { margin: 0.25rem 0 0.85rem; font-size: 1.4rem; text-align: center; }"
             "h2 { margin: 0 0 0.45rem; font-size: 1.02rem; text-align: center; }"
             ".value { font-weight: 700; }"
             ".mono { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, \"Liberation "
             "Mono\", monospace; }"
             ".green { color: #0f7b0f; }"
             ".orange { color: #b56900; }"
             ".red { color: #b42318; }"
             ".section { background: #fff; border: 1px solid #d7dde5; border-radius: 10px; margin: "
             "0 auto 0.72rem; padding: 0.72rem 0.82rem; max-width: 540px; box-shadow: 0 1px 2px "
             "rgba(16,24,40,0.04); }"
             ".kv { width: 100%; max-width: 640px; margin: 0 auto; border-collapse: collapse; }"
             ".kv td { padding: 0.2rem 0.25rem; vertical-align: top; text-align: center; }"
             ".kv td:first-child { width: 42%; color: #5b6675; }"
             ".diag-wrap { width: 100%; overflow-x: auto; -webkit-overflow-scrolling: touch; }"
             ".diag-table { width: 100%; min-width: 460px; margin: 0 auto; border-collapse: "
             "collapse; font-size: 0.94rem; }"
             ".diag-table th, .diag-table td { border-top: 1px solid #e6ebf1; padding: 0.35rem "
             "0.25rem; text-align: center; vertical-align: top; }"
             ".diag-table th { color: #4b5563; font-weight: 700; border-top: none; }"
             ".small { font-size: 0.88rem; color: #5b6675; }"
             ".section pre { margin: 0.4rem auto 0.25rem; padding: 0.45rem 0.5rem; overflow-x: "
             "auto; background: #f8fafc; border-radius: 6px; border: 1px solid #e5e7eb; "
             "text-align: left; max-width: 640px; }"
             "a { display: block; margin: 0.9rem 0 0.25rem; text-align: center; text-decoration: "
             "none; color: #0b5ed7; font-weight: 600; }"
             "@media (min-width: 700px) { body { padding: 1rem; } h1 { font-size: 1.58rem; } .kv "
             "td:first-child { width: 34%; } }"
             "@media (max-width: 560px) { .kv { max-width: 100%; } .kv td, .kv td:first-child { "
             "display: block; width: 100%; text-align: center; } .kv tr { display: block; padding: "
             "0.22rem 0; border-top: 1px solid #edf1f6; } .kv tr:first-child { border-top: none; } "
             ".diag-table { min-width: 430px; font-size: 0.89rem; } .section { max-width: 100%; } }"
             "</style></head><body><div class='container'><h1>Device Status</h1>");

    snprintf(
        buffer, sizeof(buffer),
        "<div class='section'><h2>Device And Network</h2>"
        "<table class='kv'>"
        "<tr><td>Type</td><td class='value'>%s</td></tr>"
        "<tr><td>SSID</td><td class='value'>%s</td></tr>"
        "<tr><td>MAC Address</td><td class='value mono'>%02X:%02X:%02X:%02X:%02X:%02X</td></tr>"
        "<tr><td>Wi-Fi Reconnect</td><td><span class='value'>%d min</span></td></tr>"
        "<tr><td>Wi-Fi Timeout</td><td><span class='value'>%d s</span></td></tr>"
        "<tr><td>Refresh Intervals</td><td class='mono'>[%d, %d, %d, %d, %d, %d, %d, %d]</td></tr>"
        "</table></div>",
        device_type_label, wifi_config_flash.ssid, mac_buf[0], mac_buf[1], mac_buf[2], mac_buf[3],
        mac_buf[4], mac_buf[5], device_config_flash.data.wifi_reconnect_minutes,
        device_config_flash.data.wifi_timeout,
        device_config_flash.data.refresh_minutes_by_pushbutton[0],
        device_config_flash.data.refresh_minutes_by_pushbutton[1],
        device_config_flash.data.refresh_minutes_by_pushbutton[2],
        device_config_flash.data.refresh_minutes_by_pushbutton[3],
        device_config_flash.data.refresh_minutes_by_pushbutton[4],
        device_config_flash.data.refresh_minutes_by_pushbutton[5],
        device_config_flash.data.refresh_minutes_by_pushbutton[6],
        device_config_flash.data.refresh_minutes_by_pushbutton[7]);
    strcat(page, buffer);

    snprintf(buffer, sizeof(buffer),
             "<div class='section'><h2>Time And Power</h2>"
             "<table class='kv'>"
             "<tr><td>RTC (Raw)</td><td class='value'>%02d:%02d, %s, %02d. %s %04d</td></tr>"
             "<tr><td>RTC (DST)</td><td class='value'>%s</td></tr>"
             "<tr><td>RTC Temperature</td><td class='value'>%s &deg;C</td></tr>"
             "<tr><td>MCU Temperature</td><td class='value'>%.1f &deg;C</td></tr>"
             "<tr><td>Vcc</td><td><span class='value %s'>%.3f V</span></td></tr>"
             "<tr><td>Vbat</td><td><span class='value %s'>%.3f V</span></td></tr>"
             "<tr><td>ADC Conversion</td><td class='value mono'>%.8f</td></tr>"
             "</table></div>",
             now.hours, now.minutes, get_day_of_week(now.day), now.date, get_month_name(now.month),
             2000 + now.year, buffer2, rtc_temp_str, temp_c, vcc_color, vcc, vbat_color, vbat,
             device_config_flash.data.conversion_factor);
    strcat(page, buffer);

    snprintf(buffer, sizeof(buffer),
             "<div class='section'><h2>I2C Diagnostics</h2>"
             "<div class='diag-wrap'><table class='diag-table'>"
             "<thead><tr><th>Device</th><th>Addr</th><th>Status</th></tr></thead>"
             "<tbody>"
             "<tr><td>DS3231</td><td class='mono'>0x68</td><td class='value %s'>%s</td></tr>"
             "<tr><td>BMP581</td><td class='mono'>%s</td><td class='value %s'>%s</td></tr>"
             "<tr><td>RV-3028</td><td class='mono'>0x52</td><td class='value %s'>%s</td></tr>"
             "<tr><td>ST25DV</td><td class='mono'>0x53/0x57</td><td class='value %s'>%s</td></tr>"
             "</tbody></table></div></div>",
             ds3231_color, ds3231_state, bmp581_addr_str, bmp581_color, bmp581_state, rv3028_color,
             rv3028_state, st25_color, st25_state);
    strcat(page, buffer);

    snprintf(buffer, sizeof(buffer),
             "<div class='section'><h2>Memory</h2>"
             "<table class='kv'>"
             "<tr><td>Heap Used</td><td class='value'>%u KB</td></tr>"
             "<tr><td>Heap Headroom</td><td class='value'>%u KB</td></tr>"
             "<tr><td>Stack Margin</td><td class='value'>%u KB (approx)</td></tr>"
             "<tr><td>Heap Base</td><td class='value mono'>0x%08X</td></tr>"
             "<tr><td>Heap End</td><td class='value mono'>0x%08X</td></tr>"
             "<tr><td>Stack Limit</td><td class='value mono'>0x%08X</td></tr>"
             "<tr><td>SP (core0)</td><td class='value mono'>0x%08X</td></tr>"
             "</table></div>",
             (unsigned)(mem.heap_used_bytes / 1024U), (unsigned)(mem.heap_headroom_bytes / 1024U),
             (unsigned)(mem.stack_margin_bytes / 1024U), (unsigned)mem.heap_base_addr,
             (unsigned)mem.heap_end_addr, (unsigned)mem.heap_limit_addr, (unsigned)mem.sp_addr);
    strcat(page, buffer);

    snprintf(buffer, sizeof(buffer),
             "<div class='section'><h2>Flash And Firmware</h2>"
             "<table class='kv'>"
             "<tr><td>Logo In Flash</td><td class='value %s'>%s</td></tr>"
             "<tr><td>Active Firmware</td><td class='value mono'>%s</td></tr>"
             "</table>"
             "<div class='diag-wrap'><table class='diag-table'>"
             "<thead><tr><th>Slot</th><th>Version</th><th>Use Case</th><th>Build</th><th>Size "
             "(B)</th><th>Valid</th></tr></thead>"
             "<tbody>%s%s</tbody>"
             "</table></div></div>",
             has_logo ? "green" : "red", logo_info_str, get_active_firmware_slot_info(), slot0_row,
             slot1_row);
    strcat(page, buffer);

    snprintf(buffer, sizeof(buffer), "<div class='section small'>%s</div>", timeout_info);
    strcat(page, buffer);

    strcat(page, "<a href=\"/\">back</a></div></body></html>");

    send_response(tpcb, page);
}

/**
 * @brief Generates and sends the logo upload page
 * @param tpcb TCP connection pointer
 * @param message Status message to display (success/error feedback)
 *
 * Creates an HTML page with file upload interface for logo images.
 * Includes JavaScript for client-side file validation and upload progress.
 */
void send_upload_logo_page(struct tcp_pcb *tpcb, const char *message) {
    char page[4096];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    snprintf(
        page, sizeof(page),
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        // "<meta http-equiv=\"refresh\" content=\"5\">"
        "<title>Logo Upload</title>"
        "<style>"
        "body { font-family: sans-serif; text-align: center; margin: 2em; }"
        "input[type='file'] { font-size: 1em; padding: 0.5em; margin: 0.5em auto; display: block; "
        "width: 80%%; max-width: 300px; }"
        "button { font-size: 1em; padding: 0.5em; margin: 0.5em auto; display: block; width: 80%%; "
        "max-width: 300px; }"
        "#status, #error { margin-top: 1em; font-weight: bold; color: red; }"
        "progress { width: 80%%; max-width: 300px; height: 2em; margin-top: 1em; }"
        "a { display: inline-block; margin-top: 2em; }"
        "</style>"
        "</head><body>"
        "<h1>Upload Logo</h1>"
        "%s"
        "<input type='file' id='fileInput'><br>"
        "<button onclick='upload()'>Upload</button><br>"
        "<progress id='progressBar' max='100' value='0'></progress>"
        "<p id='status'></p>"
        "<a href='/'>Back</a>"
        "<script>"
        "const MAX_SIZE = %d;" // Wird korrekt ersetzt
        "function upload() {"
        "  const file = document.getElementById('fileInput').files[0];"
        "  if (!file) return;"
        "  if (file.size > MAX_SIZE) {"
        "    document.getElementById('status').innerText = '❌ Datei zu groß (' + file.size + ' "
        "Bytes, maximal ' + MAX_SIZE + ' Bytes erlaubt)';"
        "    return;"
        "  }"
        "  const xhr = new XMLHttpRequest();"
        "  xhr.open('POST', '/upload_logo', true);"
        "  xhr.setRequestHeader('Content-Type', 'application/octet-stream');"
        "  xhr.upload.onprogress = function(e) {"
        "    if (e.lengthComputable) {"
        "      const percent = Math.round(e.loaded / e.total * 100);"
        "      document.getElementById('progressBar').value = percent;"
        "      document.getElementById('status').innerText = 'Hochladen: ' + percent + '%%';"
        "    }"
        "  };"
        "  xhr.onload = function() {"
        "    if (xhr.status == 200) document.getElementById('status').innerText = '✅ Upload OK';"
        "    else document.getElementById('status').innerText = '❌ Upload fehlgeschlagen';"
        "  };"
        "  xhr.onerror = function() {"
        "    document.getElementById('status').innerText = '❌ Fehler beim Upload';"
        "  };"
        "  xhr.send(file);"
        "}"
        "</script></body></html>",
        (message && *message) ? message : "", LOGO_FLASH_SIZE);

    int width, height, datalen;
    bool has_logo = get_flash_logo_info(&width, &height, &datalen);

    if (has_logo) {
        snprintf(page + strlen(page), sizeof(page) - strlen(page),
                 "<p><b>Benutzerdefiniertes Logo gefunden:</b> %d×%d Pixel, %d Bytes</p>\n"
                 "<form method=\"POST\" action=\"/delete_logo\">"
                 "<button type=\"submit\">delete logo</button></form>\n",
                 width, height, datalen);
    } else {
        strcat(page, "<p><i>Kein benutzerdefiniertes Logo im Flash.</i></p>\n");
    }

    snprintf(page + strlen(page), sizeof(page) - strlen(page), "<p>%s</p></body></html>",
             timeout_info);
    debug_log("upload_logo page length: %d\n", strlen(page));

    send_response(tpcb, page);
}

/**
 * @brief Generates and sends the firmware update page
 * @param tpcb TCP connection pointer
 * @param message Status message to display (upload result feedback)
 *
 * Creates an HTML page for firmware binary uploads with slot management.
 * Shows current firmware status and provides upload interface.
 */
void send_firmware_update_page(struct tcp_pcb *tpcb, const char *message) {
    // If `message` starts with "<div" or "<h2", send only the fragment
    if (message && *message &&
        (strncmp(message, "<div", 4) == 0 || strncmp(message, "<h2", 3) == 0)) {
        debug_log("Sending short message only (HTML fragment)\n");
        send_response(tpcb, message);
        return;
    }

    // Normal complete page
    char page[4096];
    char buffer[256];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));
    const int max_size = FIRMWARE_FLASH_SIZE;

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta charset=\"UTF-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
             "<title>firmware update</title>"
             "<style>"
             "body { font-family: sans-serif; text-align: center; margin: 2em; }"
             "input[type='file'] { font-size: 1em; padding: 0.5em; margin: 0.5em auto; display: "
             "block; width: 80%%; max-width: 300px; }"
             "button { font-size: 1em; padding: 0.5em; margin: 0.5em auto; display: block; width: "
             "80%%; max-width: 300px; }"
             "#status { margin-top: 1em; font-weight: bold; color: red; }"
             ".spinner { width: 48px; height: 48px; border: 6px solid #ccc; border-top-color: "
             "#4CAF50; border-radius: 50%%; margin: 1.5em auto; display: none; animation: spin 1s "
             "linear infinite; }"
             ".spinner.active { display: block; }"
             "@keyframes spin { to { transform: rotate(360deg); } }"
             "a { display: inline-block; margin-top: 2em; }"
             "</style></head><body>\n");

    strcat(page, "<h1>Firmware Update</h1>");

    // Active slot info
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>Active firmware:<br>"
             "<div><span class='value'>%s</span></div><br>",
             get_active_firmware_slot_info());
    strcat(page, buffer);

    if (message && *message) {
        strncat(page, message, sizeof(page) - strlen(page) - 1);
    }

    strcat(page, "<input type='file' id='fileInput'><br>"
                 "<button onclick='upload()'>Upload</button><br>"
                 "<div id='spinner' class='spinner'></div>"
                 "<p id='status'></p>"
                 "<div id='uploadResult'></div>"
                 "<a href='/'>Back</a>\n");

    snprintf(page + strlen(page), sizeof(page) - strlen(page),
             "<script>"
             "const MAX_SIZE = %d;"
             "function upload() {"
             "  const file = document.getElementById('fileInput').files[0];"
             "  if (!file) return;"
             "  if (file.size > MAX_SIZE) {"
             "    document.getElementById('status').innerText = '❌ File too large (' + file.size "
             "+ ' bytes, maximum ' + MAX_SIZE + ' bytes allowed)';"
             "    return;"
             "  }"
             "  const xhr = new XMLHttpRequest();"
             "  xhr.open('POST', '/firmware_update', true);"
             "  xhr.setRequestHeader('Content-Type', 'application/octet-stream');"
             "  xhr.responseType = 'text';"
             "  document.getElementById('spinner').classList.add('active');"
             "  document.getElementById('status').innerText = 'Uploading image… please wait and "
             "leave the device untouched (this may take a couple of minutes).';"
             "  document.getElementById('uploadResult').innerHTML = '';"
             "  xhr.onerror = function() {"
             "    document.getElementById('spinner').classList.remove('active');"
             "    document.getElementById('status').innerText = '❌ Upload error';"
             "  };"
             "  xhr.onload = function() {"
             "    document.getElementById('spinner').classList.remove('active');"
             "    document.getElementById('status').innerText = '';"
             "    document.getElementById('uploadResult').innerHTML = xhr.responseText;"
             "  };"
             "  xhr.send(file);"
             "}"
             "</script></body></html>",
             max_size);

    // Firmware Slot Info
    char build0[16] = {0}, version0[32] = {0};
    char build1[16] = {0}, version1[32] = {0};
    uint32_t size0 = 0, size1 = 0;
    uint32_t crc0 = 0, crc1 = 0;
    uint8_t slot_index0 = 0, slot_index1 = 0;
    uint8_t valid0 = 0, valid1 = 0;
    bool has0 = get_firmware_slot_info(0, build0, version0, &size0, &crc0, &slot_index0, &valid0);
    bool has1 = get_firmware_slot_info(1, build1, version1, &size1, &crc1, &slot_index1, &valid1);

    if (has0 || has1) {
        strcat(page, "<p><b>Firmware im Flash gefunden:</b></p><ul>\n");

        if (has0) {
            char slot0_use_case[64] = "n/a";
            format_slot_use_case(0, slot0_use_case, sizeof(slot0_use_case), false);
            snprintf(page + strlen(page), sizeof(page) - strlen(page),
                     "<div>Slot 0: %s, %s, (%s), %u Bytes</div>\n", version0, slot0_use_case,
                     build0, size0);
        } else {
            strcat(page, "<div>Slot 0: <i>empty or invalid</i></div>\n");
        }

        if (has1) {
            char slot1_use_case[64] = "n/a";
            format_slot_use_case(1, slot1_use_case, sizeof(slot1_use_case), false);
            snprintf(page + strlen(page), sizeof(page) - strlen(page),
                     "<div>Slot 1: %s, %s, (%s), %u Bytes</div>\n", version1, slot1_use_case,
                     build1, size1);
        } else {
            strcat(page, "<div>Slot 1: <i>empty or invalid</i></div>\n");
        }

        strcat(page, "</ul>\n");
    } else {
        strcat(page, "<p><i>No valid firmware found in Slot 0 or 1.</i></p>\n");
    }

    strcat(page,
           "<div style='border:2px solid #c62828; background:#fff3e0; color:#333; padding:0.9em; "
           "margin:1em auto; width:90%; max-width:560px; text-align:left;'>"
           "Demote the currently active slot metadata so the other valid slot is preferred on the "
           "next reboot. This way it is possible to boot from older builds."
           "<br><span style='color:#c62828'>Warning:</span> This rewrites firmware header metadata "
           "(version/build date) of the running slot. Use only if you understand the A/B update "
           "behavior."
           "<form method='POST' action='/firmware_demote_active' "
           "onsubmit=\"return confirm('Dangerous developer helper: demote ACTIVE slot metadata? "
           "Current firmware keeps running, but next reboot may switch to the other slot. "
           "Continue?');\" style='margin-top:0.8em;'>"
           "<button type='submit' style='background:#c62828; color:white; border:none; "
           "padding:0.6em 0.9em; border-radius:4px; cursor:pointer;'>Demote active slot priority"
           "</button>"
           "</form></div>");

    snprintf(page + strlen(page), sizeof(page) - strlen(page), "<p>%s</p></body></html>",
             timeout_info);

    debug_log("firmware_update page length: %d\n", strlen(page));
    send_response(tpcb, page);
}

/**
 * @brief Generates and sends the Wi-Fi configuration page
 * @param tpcb TCP connection pointer
 * @param message Status message to display (save confirmation/error)
 *
 * Creates an HTML form for Wi-Fi network configuration including
 * SSID, password, and SeatSurfing server connection settings.
 */
void send_wifi_config_page(struct tcp_pcb *tpcb, const char *message) {
    char page[2048];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta charset=\"UTF-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
             "<meta http-equiv=\"refresh\" content=\"300\">"
             "<title>Wi-Fi Konfiguration</title>"
             "<style>"
             "body { font-family: sans-serif; text-align: center; }"
             "form { max-width: 400px; margin: auto; padding: 1em; }"
             "label { display: block; margin-bottom: 1em; font-size: 1em; }"
             "input[type='text'] { width: 100%%; padding: 0.5em; font-size: 1em; }"
             "input[type='submit'] { padding: 0.6em 1em; font-size: 1em; margin: 0.5em; width: "
             "45%%; max-width: 150px; }"
             "a { display: inline-block; margin-top: 1.5em; font-size: 0.9em; text-decoration: "
             "none; color: #0066cc; }"
             ".message { margin: 1em auto; font-size: 1em; font-weight: bold; color: green; }"
             "</style></head><body>"
             "<h1>Wi-Fi Konfiguration</h1>");

    if (message && *message) {
        snprintf(page + strlen(page), sizeof(page) - strlen(page), "<div class='message'>%s</div>",
                 message);
    }

    snprintf(page + strlen(page), sizeof(page) - strlen(page),
             "<form method=\"POST\" action=\"/wifi\">"
             "<label>SSID:<br><input type=\"text\" name=\"text1\" value=\"%s\"></label>"
             "<label>Passwort:<br><input type=\"text\" name=\"text2\" value=\"%s\"></label>"
             "<input type=\"submit\" value=\"store\">"
             "</form>"
             "<a href=\"/\">back</a>"
             "<p>%s</p>"
             "</body></html>",
             wifi_config_flash.ssid, wifi_config_flash.password, timeout_info);

    send_response(tpcb, page);
}

#ifdef USE_CASE_SEATSURFING
/**
 * @brief Generates and sends the SeatSurfing API configuration page
 * @param tpcb TCP connection pointer
 * @param message Status message to display (save confirmation/error)
 *
 * Creates an HTML form for SeatSurfing integration settings including
 * server URL, API credentials, and location configuration.
 */
void send_seatsurfing_config_page(struct tcp_pcb *tpcb, const char *message) {
    char page[8192];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    char ip_string[16];
    snprintf(ip_string, sizeof(ip_string), "%d.%d.%d.%d", seatsurfing_config_flash.data.ip[0],
             seatsurfing_config_flash.data.ip[1], seatsurfing_config_flash.data.ip[2],
             seatsurfing_config_flash.data.ip[3]);

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta charset=\"UTF-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
             "<meta http-equiv=\"refresh\" content=\"30\">"
             "<title>Seatsurfing Konfiguration</title>"
             "<style>"
             "body { font-family: sans-serif; text-align: center; }"
             "form { max-width: 400px; margin: auto; padding: 1em; }"
             "label { display: block; margin-bottom: 1em; font-size: 1em; }"
             "label.inline { display: inline-block; margin-right: 1em; }"
             "input[type='text'], input[type='number'] { width: 100%%; padding: 0.5em; font-size: "
             "1em; box-sizing: border-box; }"
             "fieldset { border: 1px solid #ccc; padding: 1em 1.2em; margin-top: 1em; text-align: "
             "left; }"
             "legend { font-weight: bold; }"
             "input[type='submit'] { padding: 0.6em 1em; font-size: 1em; margin: 0.5em; width: "
             "45%%; max-width: 150px; }"
             "a { display: inline-block; margin-top: 1.5em; font-size: 0.9em; text-decoration: "
             "none; color: #0066cc; }"
             "</style></head><body>"
             "<h1>Seatsurfing Konfiguration</h1>");

    if (message && *message) {
        snprintf(page + strlen(page), sizeof(page) - strlen(page), "<div class='message'>%s</div>",
                 message);
    }

    snprintf(
        page + strlen(page), sizeof(page) - strlen(page),
        "<form method=\"POST\" action=\"/seatsurfing\">"
        "<fieldset><legend>Room Settings</legend>"
        "<label>Room name:<br><input type=\"text\" name=\"roomname\" value=\"%s\" "
        "maxlength=\"15\"></label>"
        "<div style=\"margin-top:1em;\">"
        "<strong>Room type</strong><br>"
        "<label class=\"inline\"><input type=\"radio\" name=\"type\" value=\"0\" %s> Office</label>"
        "<label class=\"inline\"><input type=\"radio\" name=\"type\" value=\"1\" %s> "
        "Meeting</label>"
        "<label class=\"inline\"><input type=\"radio\" name=\"type\" value=\"2\" %s> Lecture "
        "hall</label>"
        "</div>"
        "<div style=\"margin-top:1em;\">"
        "<strong>Number of seats:</strong> <span id=\"number_of_seats\">%u</span><br>"
        "<small>Number of seats is defined by filled Space Name fields below.</small>"
        "</div>"
        "</fieldset>"
        "<fieldset><legend>SeatSurfing API</legend>"
        "<label>API Host:<br><input type=\"text\" name=\"text1\" value=\"%s\"></label>"
        "<label>Benutzername:<br><input type=\"text\" name=\"text2\" value=\"%s\"></label>"
        "<label>Passwort:<br><input type=\"text\" name=\"text3\" value=\"%s\"></label>"
        "<label>IP-Adresse:<br><input type=\"text\" name=\"text4\" value=\"%s\"></label>"
        "<label>Port:<br><input type=\"text\" name=\"text5\" value=\"%d\"></label>"
        "<label>Location ID:<br><input type=\"text\" name=\"text6\" value=\"%s\"></label>"
        "<label>Space Name 1:<br><input type=\"text\" name=\"text7\" value=\"%s\" "
        "oninput=\"updateDerivedSeats()\"></label>"
        "<label>Space Name 2:<br><input type=\"text\" name=\"text8\" value=\"%s\" "
        "oninput=\"updateDerivedSeats()\"></label>"
        "<label>Space Name 3:<br><input type=\"text\" name=\"text9\" value=\"%s\" "
        "oninput=\"updateDerivedSeats()\"></label>"
        "<label>Space Name 4:<br><input type=\"text\" name=\"text10\" value=\"%s\" "
        "oninput=\"updateDerivedSeats()\"></label>"
        "</fieldset>"
        "<small>Seats are derived from filled Space Name fields and stored "
        "automatically.</small><br>"
        "<input type=\"submit\" value=\"store\">"
        "</form>"
        "<a href=\"/\">Back to Start</a>"
        "<p>%s</p>"
        "<script>"
        "function updateDerivedSeats(){"
        "  var count = 0;"
        "  for (var i = 7; i <= 10; i++) {"
        "    var el = document.querySelector('input[name=\"text' + i + '\"]');"
        "    if (el && el.value.trim().length) count++;"
        "  }"
        "  if (count === 0) count = 1;"
        "  var seats = document.getElementById('number_of_seats');"
        "  if (seats) seats.textContent = count;"
        "}"
        "window.addEventListener('load', updateDerivedSeats);"
        "</script>"
        "</body></html>",
        device_config_flash.data.roomname, (device_config_flash.data.type == 0 ? "checked" : ""),
        (device_config_flash.data.type == 1 ? "checked" : ""),
        (device_config_flash.data.type == 2 ? "checked" : ""),
        (unsigned)seatsurfing_config_flash.data.seat_count, seatsurfing_config_flash.data.host,
        seatsurfing_config_flash.data.username, seatsurfing_config_flash.data.password, ip_string,
        seatsurfing_config_flash.data.port, seatsurfing_config_flash.data.location_id,
        seatsurfing_config_flash.data.space_ids[0], seatsurfing_config_flash.data.space_ids[1],
        seatsurfing_config_flash.data.space_ids[2], seatsurfing_config_flash.data.space_ids[3],
        timeout_info);

    send_response(tpcb, page);
}
#elif defined(USE_CASE_HISTORIAN)
/**
 * @brief Generates and sends the Historian API configuration page
 * @param tpcb TCP connection pointer
 * @param message Status message to display (save confirmation/error)
 *
 * Creates an HTML form for Historian integration settings including
 * server URL, port, API path, and datapoint configuration.
 */
void send_historian_config_page(struct tcp_pcb *tpcb, const char *message) {
    char page[2048];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    snprintf(
        page, sizeof(page),
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta http-equiv=\"refresh\" content=\"30\">"
        "<title>Historian Configuration</title>"
        "<style>"
        "body { font-family: sans-serif; text-align: center; }"
        "form { max-width: 400px; margin: auto; padding: 1em; }"
        "label { display: block; margin-bottom: 1em; font-size: 1em; }"
        "input[type='text'], input[type='number'] { width: 100%%; padding: 0.5em; font-size: 1em; }"
        "input[type='submit'] { padding: 0.6em 1em; font-size: 1em; margin: 0.5em; width: 45%%; "
        "max-width: 150px; }"
        "a { display: inline-block; margin-top: 1.5em; font-size: 0.9em; text-decoration: none; "
        "color: #0066cc; }"
        "</style></head><body>"
        "<h1>Historian Configuration</h1>");

    if (message && *message) {
        snprintf(page + strlen(page), sizeof(page) - strlen(page), "<div class='message'>%s</div>",
                 message);
    }

    snprintf(
        page + strlen(page), sizeof(page) - strlen(page),
        "<form method=\"POST\" action=\"/historian\">"
        "<label>Server IP:<br><input type=\"text\" name=\"text1\" value=\"%d.%d.%d.%d\"></label>"
        "<label>Port:<br><input type=\"number\" name=\"text2\" value=\"%d\"></label>"
        "<label>API Path:<br><input type=\"text\" name=\"text3\" value=\"%s\"></label>"
        "<label>Datapoint ID:<br><input type=\"number\" name=\"text4\" value=\"%d\"></label>"
        "<label>Hours Back:<br><input type=\"number\" name=\"text5\" value=\"%d\"></label>"
        "<label>Display Name:<br><input type=\"text\" name=\"text6\" value=\"%s\"></label>"
        "<input type=\"submit\" value=\"store\">"
        "</form>"
        "<a href=\"/\">Back to Start</a>"
        "<p>%s</p>"
        "</body></html>",
        historian_config_flash.data.ip[0], historian_config_flash.data.ip[1],
        historian_config_flash.data.ip[2], historian_config_flash.data.ip[3],
        historian_config_flash.data.port, historian_config_flash.data.path,
        historian_config_flash.data.datapoint_id, historian_config_flash.data.hours_back,
        historian_config_flash.data.display_name, timeout_info);

    send_response(tpcb, page);
}
#elif defined(USE_CASE_HOMEMATIC)
/**
 * @brief Generates and sends the Homematic (HmIP) configuration page
 */
void send_homematic_config_page(struct tcp_pcb *tpcb, const char *message) {
    char page[4096];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    // Compose IP string
    char ip_string[16];
    snprintf(ip_string, sizeof(ip_string), "%d.%d.%d.%d", homematic_config_flash.data.ip[0],
             homematic_config_flash.data.ip[1], homematic_config_flash.data.ip[2],
             homematic_config_flash.data.ip[3]);

    snprintf(
        page, sizeof(page),
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta http-equiv=\"refresh\" content=\"30\">"
        "<title>Homematic Configuration</title>"
        "<style>"
        "body { font-family: sans-serif; text-align: center; }"
        "h1 { margin: .6em 0; }"
        "form { max-width: 480px; margin: auto; padding: 0.6em; }"
        "label { display: block; margin-bottom: 0.8em; font-size: .95em; text-align:left;}"
        /* Uniform input sizing and prevent overflow */
        "form input[type='text'], form input[type='number'], form select { width: 100%%; padding: "
        ".32em; font-size: .80em; box-sizing: border-box; }"
        "table input[type='text'], table input[type='number'] { width: 100%%; padding: .32em; "
        "font-size: .80em; box-sizing: border-box; }"
        "table input.key { font-size: .80em; }"
        "input[type='submit'] { padding: 0.6em 1em; font-size: .9em; margin: 0.7em; width: 45%%; "
        "max-width: 180px; }"
        "table { width:100%%; border-collapse: collapse; margin-top: 1em; table-layout: fixed; }"
        "th, td { border: 1px solid #ccc; padding: .30em; font-size:.95em; }"
        "th { background:#f5f5f5; }"
        "a { display: inline-block; margin-top: 1.5em; font-size: 0.9em; text-decoration: none; "
        "color: #0066cc; }"
        "</style></head><body>"
        "<h1>Homematic Configuration</h1>");

    // Logo rendering postponed

    if (message && *message) {
        snprintf(page + strlen(page), sizeof(page) - strlen(page), "<div class='message'>%s</div>",
                 message);
    }

    snprintf(page + strlen(page), sizeof(page) - strlen(page),
             "<form method=\"POST\" action=\"/homematic\">"
             "<label>Server IP:<br><input type=\"text\" name=\"text1\" value=\"%s\"></label>"
             "<label>Port:<br><input type=\"number\" name=\"text2\" value=\"%d\"></label>",
             ip_string, homematic_config_flash.data.port);

    // Items table with fixed column widths so inputs roughly match content
    strcat(page, "<table><colgroup>"
                 "<col style='width:6%'>"  // row number
                 "<col style='width:30%'>" // address
                 "<col style='width:38%'>" // key
                 "<col style='width:26%'>" // name
                 "</colgroup>"
                 "<tr><th>#</th><th>Address</th><th>Key</th><th>Name</th></tr>");
    for (int i = 0; i < HOMEMATIC_MAX_ITEMS; i++) {
        const char *addr = (i < homematic_config_flash.data.count)
                               ? homematic_config_flash.data.items[i].address
                               : "";
        const char *key =
            (i < homematic_config_flash.data.count) ? homematic_config_flash.data.items[i].key : "";
        const char *lab = (i < homematic_config_flash.data.count)
                              ? homematic_config_flash.data.items[i].fallback_label
                              : "";
        char row[512];
        snprintf(row, sizeof(row),
                 "<tr><td>%d</td>"
                 "<td><input type=\"text\" name=\"text%d\" value=\"%s\"></td>"
                 "<td><input class=\"key\" type=\"text\" name=\"text%d\" value=\"%s\"></td>"
                 "<td><input type=\"text\" name=\"text%d\" value=\"%s\"></td></tr>",
                 i + 1, 5 + 3 * i, addr, 6 + 3 * i, key, 7 + 3 * i, lab);
        strcat(page, row);
    }
    strcat(page, "</table>");

    snprintf(page + strlen(page), sizeof(page) - strlen(page),
             "<input type=\"submit\" value=\"store\">"
             "</form>"
             "<a href=\"/\">Back to Start</a>"
             "<p>%s</p>"
             "</body></html>",
             timeout_info);

    send_response(tpcb, page);
}
#endif

/**
 * @brief Generates and sends the RTC clock configuration page
 * @param tpcb TCP connection pointer
 * @param message Status message to display (time set confirmation/error)
 *
 * Creates an HTML form for setting the DS3231 RTC time and date.
 * Includes current time display and manual time entry fields.
 */
void send_clock_page(struct tcp_pcb *tpcb, const char *message) {
    char page[8192];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    extern ds3231_t ds3231;
    ds3231_data_t current;
    ds3231_read_current_time(&ds3231, &current);

    // Time as raw RTC value
    char current_raw[64];
    snprintf(current_raw, sizeof(current_raw), "%02d:%02d:%02d %02d.%02d.%04d", current.hours,
             current.minutes, current.seconds, current.date, current.month, current.year + 2000);

    // Zeit inkl. Sommerzeit
    char current_dst[64];
    format_rtc_time(&current, current_dst, sizeof(current_dst));

    snprintf(
        page, sizeof(page),
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta http-equiv=\"refresh\" content=\"300\">"
        "<title>Uhrzeit setzen</title>"
        "<style>"
        "body { font-family: sans-serif; text-align: center; }"
        "form { margin-top: 2em; }"
        "input[type='submit'] { padding: 0.6em 1em; font-size: 1em; margin-top: 1em; }"
        ".message { margin: 1em auto; font-size: 1em; font-weight: bold; color: green; }"
        ".section { margin: 1em 0; font-size: 1.1em; }"
        ".value { font-weight: bold; }"
        "</style></head><body>"
        "<h1>Uhrzeit setzen</h1>"

        "<div class='section'>RTC (roh): <span class='value'>%s</span></div>"
        "<div class='section'>RTC (DST): <span class='value'>%s</span></div>"

        "%s%s%s"

        "<form id=\"clockForm\" method=\"POST\" action=\"/clock\">"
        "<input type=\"hidden\" name=\"line\" id=\"line\">"
        "<p id=\"preview\">Determining local time...</p>"
        "<input type=\"submit\" value=\"Uhr stellen\">"
        "</form>"

        "<p><a href=\"/\">Back</a></p>"
        "<p>%s</p>"

        "<script>"
        "const now = new Date();"
        "const weekday = "
        "['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'][now.getDay()];"
        "const months = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];"
        "const day = now.getDate();"
        "const month = months[now.getMonth()];"
        "const year = now.getFullYear();"
        "const hour = now.getHours().toString().padStart(2,'0');"
        "const minute = now.getMinutes().toString().padStart(2,'0');"
        "const line = `${weekday}, ${day}. ${month} ${year}, ${hour}:${minute}`;"
        "document.getElementById('line').value = line;"
        "document.getElementById('preview').textContent = 'Local time: ' + line;"
        "</script>"
        "</body></html>",
        current_raw, current_dst, (message && *message) ? "<div class='message'>" : "",
        (message && *message) ? message : "", (message && *message) ? "</div>" : "", timeout_info);

    debug_log("device settings page length: %d\n", strlen(page));
    send_response(tpcb, page);
}

/**
 * @brief Generates and sends the device configuration page
 * @param tpcb TCP connection pointer
 * @param message Status message to display (save confirmation/error)
 *
 * Creates an HTML form for device-specific settings including:
 * - Room name and display type
 * - Refresh intervals for button combinations
 * - Power management and watchdog settings
 */
void send_device_config_page(struct tcp_pcb *tpcb, const char *message) {
    char page[12288];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));
    int logo_width = 0;
    int logo_height = 0;
    int logo_bytes = 0;
    bool has_logo = get_flash_logo_info(&logo_width, &logo_height, &logo_bytes);
    extern ds3231_t ds3231;
    ds3231_data_t current = {0};
    ds3231_read_current_time(&ds3231, &current);
    char current_raw[64];
    char current_dst[64];
    snprintf(current_raw, sizeof(current_raw), "%02d:%02d:%02d %02d.%02d.%04d", current.hours,
             current.minutes, current.seconds, current.date, current.month, current.year + 2000);
    format_rtc_time(&current, current_dst, sizeof(current_dst));

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta charset=\"UTF-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
             "<meta http-equiv=\"refresh\" content=\"300\">"
             "<title>Device Configuration</title>"
             "<style>"
             "body { font-family: sans-serif; text-align: center; }"
             "form { max-width: 400px; margin: auto; padding: 1em; }"
             "label { display: block; margin-bottom: 1em; font-size: 1em; text-align: left; }"
             "label.inline { display: inline-block; margin-right: 1em; }"
             "input[type='text'], input[type='number'], input[type='file'] { width: 96%%; padding: "
             "0.4em; font-size: 1em; box-sizing: border-box; }"
             "input[type='checkbox'], input[type='radio'] { width: auto; }"
             "input[type='submit'] { padding: 0.6em 1em; font-size: 1em; margin: 0.5em; width: "
             "60%%; max-width: 200px; }"
             "button.inline-btn { width: 96%%; padding: 0.6em 1em; font-size: 1em; margin-top: "
             "0.4em; box-sizing: border-box; }"
             "button.inline-btn.danger { background: #c62828; color: white; border: 1px solid "
             "#9e1f1f; }"
             "#logoProgress { width: 96%%; height: 1.3em; margin-top: 0.5em; }"
             ".logo-status { margin-top: 0.5em; font-weight: bold; }"
             ".small-note { margin-top: 0.4em; font-size: 0.9em; color: #555; }"
             "fieldset { border: 1px solid #ccc; padding: 1em 1.2em; margin-top: 1em; text-align: "
             "left; }"
             "legend { font-weight: bold; }"
             ".message { margin: 1em auto; font-size: 1em; font-weight: bold; color: green; }"
             "</style></head><body>"
             "<h1>Device Configuration</h1>");

    if (message && *message) {
        snprintf(page + strlen(page), sizeof(page) - strlen(page), "<div class='message'>%s</div>",
                 message);
    }

    // Start of the form with general configuration grouped in a fieldset
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<form method=\"POST\" action=\"/device_config\">");

#ifndef USE_CASE_SEATSURFING
    // Simplified for Historian/Homematic: only Name & Title (stored in roomname)
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<fieldset><legend>Name & Title</legend>"
             "<label>Display title:<br>"
             "<input type=\"text\" name=\"roomname\" value=\"%s\" maxlength=\"15\"></label>"
             "</fieldset>",
             device_config_flash.data.roomname);
#endif

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<fieldset><legend>ePaper-Typ</legend>"
             "<label class=\"inline\"><input type=\"radio\" name=\"epapertype\" value=\"0\" %s> "
             "None</label>"
             "<label class=\"inline\"><input type=\"radio\" name=\"epapertype\" value=\"1\" %s> "
             "7.5 Zoll</label>"
             "<label class=\"inline\"><input type=\"radio\" name=\"epapertype\" value=\"2\" %s> "
             "4.2 Zoll</label>"
             "</fieldset>",
             (device_config_flash.data.epapertype == 0 ? "checked" : ""),
             (device_config_flash.data.epapertype == 1 ? "checked" : ""),
             (device_config_flash.data.epapertype == 2 ? "checked" : ""));

    // Refresh-Intervalle
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<fieldset><legend>Refresh Intervals (minutes)</legend>");

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page (0): <input type=\"number\" name=\"refresh0\" value=\"%d\" min=\"1\" "
             "max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[0]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 1: <input type=\"number\" name=\"refresh1\" value=\"%d\" min=\"1\" "
             "max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[1]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 2: <input type=\"number\" name=\"refresh2\" value=\"%d\" min=\"1\" "
             "max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[2]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 3: <input type=\"number\" name=\"refresh3\" value=\"%d\" min=\"1\" "
             "max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[3]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 4: <input type=\"number\" name=\"refresh4\" value=\"%d\" min=\"1\" "
             "max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[4]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 5: <input type=\"number\" name=\"refresh5\" value=\"%d\" min=\"1\" "
             "max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[5]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 6: <input type=\"number\" name=\"refresh6\" value=\"%d\" min=\"1\" "
             "max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[6]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 7: <input type=\"number\" name=\"refresh7\" value=\"%d\" min=\"1\" "
             "max=\"1440\"></label>",
             device_config_flash.data.refresh_minutes_by_pushbutton[7]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page), "</fieldset>");

    // WiFi Settings section
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<fieldset><legend>WiFi Settings</legend>"

             "<label>SSID:<br>"
             "<input type=\"text\" name=\"wifi_ssid\" value=\"%s\"></label>"

             "<label>Password:<br>"
             "<input type=\"text\" name=\"wifi_password\" value=\"%s\"></label>"

             // Number of WiFi attempts
             "<label>Number of WiFi Attempts:<br>"
             "<input type=\"number\" name=\"number_wifi_attempts\" value=\"%d\" min=\"1\" "
             "max=\"50\"></label>"

             "<label>WiFi Timeout (ms):<br>"
             "<input type=\"number\" name=\"wifi_timeout\" value=\"%d\" min=\"100\" "
             "max=\"10000\"></label>"

             "<label>Max Wait for Data (ms):<br>"
             "<input type=\"number\" name=\"max_wait_data_wifi\" value=\"%d\" min=\"10\" "
             "max=\"10000\"></label>"

             "<label>WiFi Reconnect Minutes:<br>"
             "<input type=\"number\" name=\"wifi_reconnect_minutes\" value=\"%d\" min=\"1\" "
             "max=\"30\"></label>"

             "</fieldset>",
             wifi_config_flash.ssid, wifi_config_flash.password,
             device_config_flash.data.number_wifi_attempts, device_config_flash.data.wifi_timeout,
             device_config_flash.data.max_wait_data_wifi,
             device_config_flash.data.wifi_reconnect_minutes);

    // Telemetry (optional inki-monitor companion)
    snprintf(
        strchr(page, '\0'), sizeof(page) - strlen(page),
        "<fieldset><legend>Telemetry (inki-monitor)</legend>"
        "<label class=\"inline\"><input type=\"checkbox\" name=\"telemetry_enabled\" "
        "value=\"1\" %s> Enable telemetry</label>"
        "<label>Telemetry Host (IPv4):<br>"
        "<input type=\"text\" name=\"telemetry_host\" value=\"%s\" maxlength=\"31\"></label>"
        "<label>Telemetry Port:<br>"
        "<input type=\"text\" name=\"telemetry_port\" value=\"%u\" maxlength=\"5\"></label>"
        "<label>Bearer Token:<br>"
        "<input type=\"text\" name=\"telemetry_token\" value=\"%s\" maxlength=\"95\"></label>"
        "<label>Telemetry Timeout (ms):<br>"
        "<input type=\"text\" name=\"telemetry_timeout_ms\" value=\"%d\" maxlength=\"5\"></label>"
        "<label>Telemetry Label (optional):<br>"
        "<input type=\"text\" name=\"telemetry_label\" value=\"%s\" maxlength=\"31\"></label>"
        "</fieldset>",
        (device_config_flash.data.telemetry_enabled ? "checked" : ""),
        (device_config_flash.data.telemetry_host[0] != '\0')
            ? device_config_flash.data.telemetry_host
            : "192.168.178.85",
        (unsigned)device_config_flash.data.telemetry_port, device_config_flash.data.telemetry_token,
        device_config_flash.data.telemetry_timeout_ms, device_config_flash.data.telemetry_label);

    // Hardware settings section
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<fieldset><legend>Hardware</legend>"

             // Battery cutoff voltage
             "<label>Battery Cutoff Voltage (V):<br>"
             "<input type=\"number\" step=\"0.1\" name=\"switch_off_battery_voltage\" "
             "value=\"%.2f\" min=\"2.4\" max=\"3.9\"></label><br>"

             // Watchdog timeout
             "<label>Watchdog Timeout (ms):<br>"
             "<input type=\"number\" name=\"watchdog_time\" value=\"%d\" min=\"6000\" "
             "max=\"8000\"></label><br>"

             // Conversion factor
             "<label>Conversion Factor:<br>"
             "<input type=\"text\" name=\"conversion_factor\" value=\"%.6f\" step=\"any\"></label>"

             "</fieldset>",
             device_config_flash.data.switch_off_battery_voltage,
             device_config_flash.data.watchdog_time, device_config_flash.data.conversion_factor);

    // Checkboxes
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<div style=\"margin-top: 1em;\">"
             "<label class=\"inline\"><input type=\"checkbox\" name=\"show_query_date\" "
             "value=\"1\" %s> Show query timestamp</label><br>"
             "<label class=\"inline\"><input type=\"checkbox\" name=\"query_only_at_officehours\" "
             "value=\"1\" %s> Query only during office hours</label><br>"
             "</div>",
             (device_config_flash.data.show_query_date ? "checked" : ""),
             (device_config_flash.data.query_only_at_officehours ? "checked" : ""));

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<fieldset><legend>Clock</legend>"
             "<div class=\"small-note\">RTC (raw): <b>%s</b></div>"
             "<div class=\"small-note\">RTC (DST): <b>%s</b></div>"
             "<div id=\"clockPreview\" class=\"small-note\">Local time: determining...</div>"
             "<button type=\"button\" class=\"inline-btn\" onclick=\"setClockInline()\">Set clock "
             "from browser time</button>"
             "<div id=\"clockStatus\" class=\"logo-status\"></div>"
             "</fieldset>",
             current_raw, current_dst);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<fieldset><legend>Logo</legend>"
             "<label>Logo file:<br><input type=\"file\" id=\"logoFileInput\"></label>"
             "<button type=\"button\" class=\"inline-btn\" onclick=\"uploadLogoInline()\">Upload "
             "logo</button>"
             "<progress id=\"logoProgress\" max=\"100\" value=\"0\"></progress>"
             "<div id=\"logoStatus\" class=\"logo-status\"></div>"
             "<div class=\"small-note\">Max file size: %d bytes</div>",
             LOGO_FLASH_SIZE);

    if (has_logo) {
        snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
                 "<div class=\"small-note\"><b>Stored logo:</b> %d x %d px, %d bytes</div>"
                 "<button type=\"button\" class=\"inline-btn danger\" "
                 "onclick=\"deleteLogoInline()\">Delete logo</button>",
                 logo_width, logo_height, logo_bytes);
    } else {
        snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
                 "<div class=\"small-note\"><i>No custom logo stored.</i></div>");
    }

    // Submit, Footer
    snprintf(
        strchr(page, '\0'), sizeof(page) - strlen(page),
        "</fieldset>"
        "<br>"
        "<input type=\"submit\" value=\"Store\">"
        "</form>"
        "<a href=\"/\">back</a>"
        "<p>%s</p>"
        "<script>"
        "const LOGO_MAX_SIZE = %d;"
        "function setLogoStatus(msg, color) {"
        "  const e = document.getElementById('logoStatus');"
        "  if (!e) return;"
        "  e.textContent = msg;"
        "  e.style.color = color || '#111';"
        "}"
        "function setClockStatus(msg, color) {"
        "  const e = document.getElementById('clockStatus');"
        "  if (!e) return;"
        "  e.textContent = msg;"
        "  e.style.color = color || '#111';"
        "}"
        "function buildClockLine() {"
        "  const now = new Date();"
        "  const weekday = "
        "['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'][now.getDay()];"
        "  const months = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov',"
        "'Dec'];"
        "  const day = now.getDate();"
        "  const month = months[now.getMonth()];"
        "  const year = now.getFullYear();"
        "  const hour = now.getHours().toString().padStart(2,'0');"
        "  const minute = now.getMinutes().toString().padStart(2,'0');"
        "  return `${weekday}, ${day}. ${month} ${year}, ${hour}:${minute}`;"
        "}"
        "function refreshClockPreview() {"
        "  const e = document.getElementById('clockPreview');"
        "  const line = buildClockLine();"
        "  if (e) e.textContent = 'Local time: ' + line;"
        "  return line;"
        "}"
        "function setClockInline() {"
        "  const line = refreshClockPreview();"
        "  const xhr = new XMLHttpRequest();"
        "  xhr.open('POST', '/clock', true);"
        "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
        "  xhr.onload = function() {"
        "    if (xhr.status === 200 && xhr.responseText.indexOf('❌') === -1) {"
        "      setClockStatus('Clock set.', 'green');"
        "    } else {"
        "      setClockStatus('Clock update failed.', '#c62828');"
        "    }"
        "  };"
        "  xhr.onerror = function() { setClockStatus('Clock update error.', '#c62828'); };"
        "  setClockStatus('Setting clock...', '#111');"
        "  xhr.send('line=' + encodeURIComponent(line));"
        "}"
        "function uploadLogoInline() {"
        "  const file = document.getElementById('logoFileInput').files[0];"
        "  const progress = document.getElementById('logoProgress');"
        "  if (!file) { setLogoStatus('Please select a file first.', '#c62828'); return; }"
        "  if (file.size > LOGO_MAX_SIZE) {"
        "    setLogoStatus('File too large (' + file.size + ' bytes, max ' + LOGO_MAX_SIZE + "
        "' bytes).', '#c62828');"
        "    return;"
        "  }"
        "  const xhr = new XMLHttpRequest();"
        "  xhr.open('POST', '/upload_logo', true);"
        "  xhr.setRequestHeader('Content-Type', 'application/octet-stream');"
        "  xhr.upload.onprogress = function(ev) {"
        "    if (ev.lengthComputable && progress) {"
        "      progress.value = Math.round(ev.loaded / ev.total * 100);"
        "    }"
        "  };"
        "  xhr.onload = function() {"
        "    if (xhr.status === 200) {"
        "      setLogoStatus('Logo uploaded.', 'green');"
        "    } else {"
        "      setLogoStatus('Upload failed.', '#c62828');"
        "    }"
        "  };"
        "  xhr.onerror = function() { setLogoStatus('Upload error.', '#c62828'); };"
        "  setLogoStatus('Uploading...', '#111');"
        "  if (progress) progress.value = 0;"
        "  xhr.send(file);"
        "}"
        "function deleteLogoInline() {"
        "  if (!confirm('Delete stored logo?')) return;"
        "  const xhr = new XMLHttpRequest();"
        "  xhr.open('POST', '/delete_logo', true);"
        "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
        "  xhr.onload = function() {"
        "    if (xhr.status === 200) {"
        "      setLogoStatus('Logo deleted.', 'green');"
        "    } else {"
        "      setLogoStatus('Delete failed.', '#c62828');"
        "    }"
        "  };"
        "  xhr.onerror = function() { setLogoStatus('Delete error.', '#c62828'); };"
        "  setLogoStatus('Deleting...', '#111');"
        "  xhr.send('delete=1');"
        "}"
        "refreshClockPreview();"
        "</script>"
        "</body></html>",
        timeout_info, LOGO_FLASH_SIZE);

    debug_log("device settings page length: %d\n", strlen(page));
    send_response(tpcb, page);
}

static const char *current_use_case_name(void) {
#if defined(USE_CASE_SEATSURFING)
    return "seatsurfing";
#elif defined(USE_CASE_HISTORIAN)
    return "historian";
#elif defined(USE_CASE_HOMEMATIC)
    return "homematic";
#elif defined(USE_CASE_WEATHERMAP)
    return "weathermap";
#else
    return "unknown";
#endif
}

static void format_settings_version_tag(char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }

    const char *src = (version && version[0] != '\0') ? version : "unknown";
    if (src[0] == 'v' || src[0] == 'V') {
        src++;
    }

    char cleaned[64];
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < sizeof(cleaned); i++) {
        char c = src[i];
        bool ok = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '.' || c == '-' || c == '_');
        cleaned[j++] = ok ? c : '_';
    }
    cleaned[j] = '\0';
    if (cleaned[0] == '\0') {
        strncpy(cleaned, "unknown", sizeof(cleaned) - 1);
        cleaned[sizeof(cleaned) - 1] = '\0';
    }

    snprintf(out, out_len, "inki_settings_v%s", cleaned);
}

static void format_settings_export_date(char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }

    // build_date format: "YYYY-MM-DD HH:MM:SS"
    if (build_date && strlen(build_date) >= 10) {
        snprintf(out, out_len, "%c%c%c%c%c%c%c%c", build_date[0], build_date[1], build_date[2],
                 build_date[3], build_date[5], build_date[6], build_date[8], build_date[9]);
        return;
    }

    strncpy(out, "00000000", out_len - 1);
    out[out_len - 1] = '\0';
}

static bool parse_bool_value(const char *s, bool *out) {
    if (!s || !out)
        return false;
    if (strcmp(s, "1") == 0 || strcmp(s, "true") == 0 || strcmp(s, "yes") == 0 ||
        strcmp(s, "on") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(s, "0") == 0 || strcmp(s, "false") == 0 || strcmp(s, "no") == 0 ||
        strcmp(s, "off") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool parse_ipv4_value(const char *s, uint8_t out_ip[4]) {
    int a, b, c, d;
    if (sscanf(s, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
        return false;
    }
    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) {
        return false;
    }
    out_ip[0] = (uint8_t)a;
    out_ip[1] = (uint8_t)b;
    out_ip[2] = (uint8_t)c;
    out_ip[3] = (uint8_t)d;
    return true;
}

static bool extract_urlencoded_field(const char *body, size_t len, const char *key, char *out,
                                     size_t out_len) {
    if (!body || !key || !out || out_len == 0) {
        return false;
    }
    out[0] = '\0';

    size_t key_len = strlen(key);
    const char *ptr = body;
    const char *end = body + len;

    while (ptr < end) {
        const char *amp = memchr(ptr, '&', (size_t)(end - ptr));
        const char *seg_end = amp ? amp : end;
        const char *eq = memchr(ptr, '=', (size_t)(seg_end - ptr));
        if (eq) {
            size_t klen = (size_t)(eq - ptr);
            if (klen == key_len && strncmp(ptr, key, key_len) == 0) {
                size_t vlen = (size_t)(seg_end - (eq + 1));
                char encoded[4096];
                if (vlen >= sizeof(encoded)) {
                    vlen = sizeof(encoded) - 1;
                }
                memcpy(encoded, eq + 1, vlen);
                encoded[vlen] = '\0';
                url_decode(out, encoded, out_len);
                return true;
            }
        }
        if (!amp) {
            break;
        }
        ptr = amp + 1;
    }
    return false;
}

static void append_htmlf(char *dst, size_t dst_len, size_t *off, const char *fmt, ...) {
    if (!dst || !off || *off >= dst_len) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(dst + *off, dst_len - *off, fmt, args);
    va_end(args);

    if (written < 0) {
        return;
    }
    if ((size_t)written >= dst_len - *off) {
        *off = dst_len - 1;
        dst[*off] = '\0';
        return;
    }
    *off += (size_t)written;
}

static int find_key_index(const char *key, const char *const *keys, size_t key_count) {
    for (size_t i = 0; i < key_count; i++) {
        if (strcmp(key, keys[i]) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static bool is_import_metadata_key(const char *key) {
    if (!key) {
        return false;
    }
    return strcmp(key, "contains_secrets") == 0 || strcmp(key, "device.mac") == 0 ||
           strcmp(key, "build_signature") == 0 || strcmp(key, "build_date") == 0 ||
           strncmp(key, "meta.", 5) == 0;
}

static void append_key_report_section(char *report, size_t report_len, size_t *off,
                                      const char *title, const char *const *keys, size_t key_count,
                                      const bool *seen, const bool *issues, bool section_touched,
                                      int *ok_count, int *issue_count, int *old_count) {
    if (key_count == 0) {
        return;
    }
    append_htmlf(report, report_len, off, "<h3>%s</h3><ul>", title);
    for (size_t i = 0; i < key_count; i++) {
        if (seen[i]) {
            if (issues[i]) {
                append_htmlf(report, report_len, off,
                             "<li class='status-issue'><code>%s</code> invalid value (kept "
                             "old)</li>",
                             keys[i]);
                (*issue_count)++;
            } else {
                append_htmlf(report, report_len, off,
                             "<li class='status-ok'><code>%s</code> imported</li>", keys[i]);
                (*ok_count)++;
            }
        } else if (section_touched) {
            append_htmlf(report, report_len, off,
                         "<li class='status-issue'><code>%s</code> missing in import (kept "
                         "old)</li>",
                         keys[i]);
            (*issue_count)++;
        } else {
            append_htmlf(report, report_len, off,
                         "<li class='status-old'><code>%s</code> kept old (not in file)</li>",
                         keys[i]);
            (*old_count)++;
        }
    }
    append_htmlf(report, report_len, off, "</ul>");
}

static void send_settings_transfer_page_with_report(struct tcp_pcb *tpcb, const char *message,
                                                    const char *report_html) {
    char page[8192];
    char timeout_info[64];
    const char *msg_open = "";
    const char *msg_close = "";
    add_timeout_info(timeout_info, sizeof(timeout_info));

    if (message && *message) {
        if (strstr(message, "❌") != NULL) {
            msg_open = "<div class='message message-error'>";
        } else if (strstr(message, "⚠") != NULL) {
            msg_open = "<div class='message message-warn'>";
        } else {
            msg_open = "<div class='message message-ok'>";
        }
        msg_close = "</div>";
    }

    snprintf(
        page, sizeof(page),
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta http-equiv=\"refresh\" content=\"300\">"
        "<title>Settings Import/Export</title>"
        "<style>"
        "body { font-family: sans-serif; text-align: center; }"
        ".wrap { max-width: 520px; margin: 0 auto; padding: 1em; }"
        ".card { max-width: 420px; border: 1px solid #ddd; border-radius: 8px; padding: 0.85em; "
        "margin: 1em auto; }"
        ".card h2 { margin: 0 0 0.6em 0; font-size: 1.1em; }"
        ".btn { display: inline-block; padding: 0.6em 1em; margin: 0.35em; border: 1px solid #777; "
        "border-radius: 6px; background: #f3f3f3; color: #000; text-decoration: none; cursor: "
        "pointer; }"
        ".btn:disabled, .btn.disabled { opacity: 0.5; cursor: not-allowed; }"
        ".filemeta { font-size: 0.95em; color: #333; margin-top: 0.35em; min-height: 1.2em; }"
        ".message { margin: 1em auto; font-weight: bold; }"
        ".message-ok { color: green; }"
        ".message-warn { color: #a65f00; }"
        ".message-error { color: #b00020; }"
        ".report { max-width: 420px; margin: 1em auto; text-align: left; border: 1px solid #ddd; "
        "border-radius: 8px; padding: 0.85em; }"
        ".report h2 { margin: 0 0 0.5em 0; font-size: 1.05em; text-align: center; }"
        ".report ul { margin: 0.4em 0 0.8em 1.1em; padding: 0; }"
        ".report li { margin: 0.25em 0; }"
        ".status-ok { color: #086b1a; }"
        ".status-issue { color: #b00020; }"
        ".status-old { color: #b26a00; }"
        ".report-summary { margin-top: 0.5em; font-size: 0.95em; }"
        ".hidden { display: none; }"
        "</style></head><body>"
        "<div class='wrap'>"
        "<h1>Settings Import/Export</h1>"
        "<p>Use case: <b>%s</b></p>"
        "%s%s%s"
        "<div class='card'>"
        "<h2>Export settings</h2>"
        "<a class='btn' href=\"/settings_export.txt\">Export settings file</a>"
        "<div class='filemeta'>Contains credentials/secrets. Handle this file as sensitive.</div>"
        "</div>"
        "<div class='card'>"
        "<h2>Import settings</h2>"
        "<form id=\"settingsImportForm\" method=\"POST\" action=\"/settings_import\">"
        "<input class='hidden' type=\"file\" id=\"settingsFile\" accept=\".txt,text/plain\" "
        "required>"
        "<button type='button' class='btn' id='pickFileBtn'>Choose file</button>"
        "<div class='filemeta' id='fileName'>No file selected.</div>"
        "<textarea id=\"settings\" name=\"settings\" style=\"display:none\"></textarea>"
        "<button class='btn' id='importBtn' type=\"submit\" disabled>Import settings file</button>"
        "</form>"
        "</div>"
        "%s"
        "<p><a href=\"/\">back</a></p>"
        "<p>%s</p>"
        "</div>"
        "<script>"
        "const form=document.getElementById('settingsImportForm');"
        "const f=document.getElementById('settingsFile');"
        "const pick=document.getElementById('pickFileBtn');"
        "const fileName=document.getElementById('fileName');"
        "const importBtn=document.getElementById('importBtn');"
        "const t=document.getElementById('settings');"
        "pick.addEventListener('click',()=>f.click());"
        "f.addEventListener('change',()=>{"
        " const file=f.files&&f.files[0];"
        " if(!file){ fileName.textContent='No file selected.'; importBtn.disabled=true; return; }"
        " fileName.textContent=file.name;"
        " importBtn.disabled=false;"
        "});"
        "form.addEventListener('submit',(e)=>{"
        " e.preventDefault();"
        " const file=f.files&&f.files[0];"
        " if(!file){ alert('Select a settings file first.'); return; }"
        " importBtn.disabled=true;"
        " const r=new FileReader();"
        " r.onload=()=>{ t.value=String(r.result||''); form.submit(); };"
        " r.onerror=()=>{ importBtn.disabled=false; alert('Failed to read file.'); };"
        " r.readAsText(file);"
        "});"
        "</script>"
        "</body></html>",
        current_use_case_name(), msg_open, (message && *message) ? message : "", msg_close,
        (report_html && *report_html) ? report_html : "", timeout_info);

    send_response(tpcb, page);
}

void send_settings_transfer_page(struct tcp_pcb *tpcb, const char *message) {
    send_settings_transfer_page_with_report(tpcb, message, NULL);
}

void send_settings_export_txt(struct tcp_pcb *tpcb) {
    char out[8192];
    char settings_tag[80];
    char settings_date[16];
    char download_name[128];
    char content_disposition[192];
    char source_device_id[32];
    char exported_at[32];
    size_t off = 0;

    format_settings_version_tag(settings_tag, sizeof(settings_tag));
    format_settings_export_date(settings_date, sizeof(settings_date));
    snprintf(download_name, sizeof(download_name), "inki_settings_%s_%s.txt",
             current_use_case_name(), settings_date);
    snprintf(content_disposition, sizeof(content_disposition), "attachment; filename=\"%s\"",
             download_name);
    snprintf(source_device_id, sizeof(source_device_id), "inki-%02X%02X%02X%02X%02X%02X",
             mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4],
             mac_address[5]);
    strncpy(exported_at, "unknown", sizeof(exported_at) - 1);
    exported_at[sizeof(exported_at) - 1] = '\0';
    extern ds3231_t ds3231;
    ds3231_data_t now = {0};
    if (ds3231_read_current_time(&ds3231, &now) == 0) {
        format_rtc_time(&now, exported_at, sizeof(exported_at));
    }

    off += (size_t)snprintf(out + off, sizeof(out) - off, "%s\n", settings_tag);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "use_case=%s\n", current_use_case_name());
    off += (size_t)snprintf(out + off, sizeof(out) - off, "contains_secrets=1\n");
    off += (size_t)snprintf(out + off, sizeof(out) - off, "meta.source_device_id=%s\n",
                            source_device_id);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "meta.exported_at=%s\n", exported_at);
    off += (size_t)snprintf(
        out + off, sizeof(out) - off, "device.mac=%02X:%02X:%02X:%02X:%02X:%02X\n", mac_address[0],
        mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5]);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "build_signature=%s\n",
                            (version && version[0] != '\0') ? version : "unknown");
    off += (size_t)snprintf(out + off, sizeof(out) - off, "build_date=%s\n",
                            (build_date && build_date[0] != '\0') ? build_date : "unknown");
    off += (size_t)snprintf(out + off, sizeof(out) - off, "\n");
    off += (size_t)snprintf(out + off, sizeof(out) - off, "wifi.ssid=%s\n", wifi_config_flash.ssid);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "wifi.password=%s\n",
                            wifi_config_flash.password);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "\n");
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.roomname=%s\n",
                            device_config_flash.data.roomname);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.type=%d\n",
                            (int)device_config_flash.data.type);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.epapertype=%d\n",
                            (int)device_config_flash.data.epapertype);
    for (int i = 0; i < 8; i++) {
        off += (size_t)snprintf(out + off, sizeof(out) - off, "device.refresh%d=%d\n", i,
                                device_config_flash.data.refresh_minutes_by_pushbutton[i]);
    }
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.show_query_date=%d\n",
                            device_config_flash.data.show_query_date ? 1 : 0);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.query_only_at_officehours=%d\n",
                            device_config_flash.data.query_only_at_officehours ? 1 : 0);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.wifi_reconnect_minutes=%d\n",
                            device_config_flash.data.wifi_reconnect_minutes);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.watchdog_time=%d\n",
                            device_config_flash.data.watchdog_time);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.number_wifi_attempts=%d\n",
                            device_config_flash.data.number_wifi_attempts);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.wifi_timeout=%d\n",
                            device_config_flash.data.wifi_timeout);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.max_wait_data_wifi=%d\n",
                            device_config_flash.data.max_wait_data_wifi);
    off +=
        (size_t)snprintf(out + off, sizeof(out) - off, "device.switch_off_battery_voltage=%.3f\n",
                         device_config_flash.data.switch_off_battery_voltage);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.conversion_factor=%.6f\n",
                            device_config_flash.data.conversion_factor);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.telemetry_enabled=%d\n",
                            device_config_flash.data.telemetry_enabled ? 1 : 0);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.telemetry_host=%s\n",
                            device_config_flash.data.telemetry_host);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.telemetry_port=%u\n",
                            (unsigned)device_config_flash.data.telemetry_port);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.telemetry_token=%s\n",
                            device_config_flash.data.telemetry_token);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.telemetry_timeout_ms=%d\n",
                            device_config_flash.data.telemetry_timeout_ms);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "device.telemetry_label=%s\n",
                            device_config_flash.data.telemetry_label);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "\n");

#if defined(USE_CASE_SEATSURFING)
    off += (size_t)snprintf(out + off, sizeof(out) - off, "seatsurfing.host=%s\n",
                            seatsurfing_config_flash.data.host);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "seatsurfing.username=%s\n",
                            seatsurfing_config_flash.data.username);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "seatsurfing.password=%s\n",
                            seatsurfing_config_flash.data.password);
    off +=
        (size_t)snprintf(out + off, sizeof(out) - off, "seatsurfing.ip=%u.%u.%u.%u\n",
                         seatsurfing_config_flash.data.ip[0], seatsurfing_config_flash.data.ip[1],
                         seatsurfing_config_flash.data.ip[2], seatsurfing_config_flash.data.ip[3]);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "seatsurfing.port=%u\n",
                            (unsigned)seatsurfing_config_flash.data.port);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "seatsurfing.location_id=%s\n",
                            seatsurfing_config_flash.data.location_id);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "seatsurfing.seat_count=%u\n",
                            (unsigned)seatsurfing_config_flash.data.seat_count);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "seatsurfing.space1=%s\n",
                            seatsurfing_config_flash.data.space_ids[0]);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "seatsurfing.space2=%s\n",
                            seatsurfing_config_flash.data.space_ids[1]);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "seatsurfing.space3=%s\n",
                            seatsurfing_config_flash.data.space_ids[2]);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "seatsurfing.space4=%s\n",
                            seatsurfing_config_flash.data.space_ids[3]);
#elif defined(USE_CASE_HISTORIAN)
    off += (size_t)snprintf(out + off, sizeof(out) - off, "historian.ip=%u.%u.%u.%u\n",
                            historian_config_flash.data.ip[0], historian_config_flash.data.ip[1],
                            historian_config_flash.data.ip[2], historian_config_flash.data.ip[3]);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "historian.port=%u\n",
                            (unsigned)historian_config_flash.data.port);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "historian.path=%s\n",
                            historian_config_flash.data.path);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "historian.datapoint_id=%d\n",
                            historian_config_flash.data.datapoint_id);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "historian.hours_back=%d\n",
                            historian_config_flash.data.hours_back);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "historian.display_name=%s\n",
                            historian_config_flash.data.display_name);
#elif defined(USE_CASE_HOMEMATIC)
    off += (size_t)snprintf(out + off, sizeof(out) - off, "homematic.ip=%u.%u.%u.%u\n",
                            homematic_config_flash.data.ip[0], homematic_config_flash.data.ip[1],
                            homematic_config_flash.data.ip[2], homematic_config_flash.data.ip[3]);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "homematic.port=%u\n",
                            (unsigned)homematic_config_flash.data.port);
    off += (size_t)snprintf(out + off, sizeof(out) - off, "homematic.count=%u\n",
                            (unsigned)homematic_config_flash.data.count);
    for (int i = 0; i < HOMEMATIC_MAX_ITEMS; i++) {
        off += (size_t)snprintf(out + off, sizeof(out) - off, "homematic.item%d.address=%s\n",
                                i + 1, homematic_config_flash.data.items[i].address);
        off += (size_t)snprintf(out + off, sizeof(out) - off, "homematic.item%d.key=%s\n", i + 1,
                                homematic_config_flash.data.items[i].key);
        off += (size_t)snprintf(out + off, sizeof(out) - off, "homematic.item%d.label=%s\n", i + 1,
                                homematic_config_flash.data.items[i].fallback_label);
    }
#endif

    if (off >= sizeof(out)) {
        off = sizeof(out) - 1;
    }
    out[off] = '\0';
    send_response_with_content_type_and_disposition(tpcb, out, "text/plain; charset=UTF-8",
                                                    content_disposition);
}

// =============================================================================
// FORM PROCESSING HANDLERS
// =============================================================================

/**
 * @brief Processes Wi-Fi configuration form submissions
 * @param tpcb TCP connection pointer
 * @param body Form data from HTTP POST request
 * @param len Length of form data
 *
 * Parses Wi-Fi settings form, validates input, and saves configuration
 * to flash memory. Sends confirmation page with result status.
 */
void handle_form_wifi(struct tcp_pcb *tpcb, const char *body, size_t len) {
    web_submission_t result = {0};
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    parse_form_fields(body, len, &result);

    wifi_config_t new_cfg = {.crc32 = 0};
    strncpy(new_cfg.ssid, result.text[0], sizeof(new_cfg.ssid) - 1);
    strncpy(new_cfg.password, result.text[1], sizeof(new_cfg.password) - 1);

    bool ok = save_wifi_config(&new_cfg);

    send_wifi_config_page(tpcb, "✔ WiFi data saved");

    if (ok) {
        debug_log_with_color(COLOR_YELLOW, "SSID & password saved\n");
    } else {
        debug_log_with_color(COLOR_RED, "Error saving data\n");
    }
}

#ifdef USE_CASE_SEATSURFING
/**
 * @brief Processes SeatSurfing API configuration form submissions
 * @param tpcb TCP connection pointer
 * @param body Form data from HTTP POST request
 * @param len Length of form data
 *
 * Parses SeatSurfing integration settings, validates server connection,
 * and saves configuration to flash memory.
 */
void handle_form_seatsurfing(struct tcp_pcb *tpcb, const char *body, size_t len) {
    webserver_set_shutdown_time(make_timeout_time_ms(USER_INTERACTION_TIMEOUT_MS));

    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    web_submission_t result = {0};

    parse_form_fields(body, len, &result);

    seatsurfing_config_t new_cfg = {.crc32 = 0};

    strncpy(new_cfg.data.host, result.text[0], sizeof(new_cfg.data.host) - 1);
    strncpy(new_cfg.data.username, result.text[1], sizeof(new_cfg.data.username) - 1);
    strncpy(new_cfg.data.password, result.text[2], sizeof(new_cfg.data.password) - 1);

    int ip0, ip1, ip2, ip3;
    if (sscanf(result.text[3], "%d.%d.%d.%d", &ip0, &ip1, &ip2, &ip3) == 4) {
        new_cfg.data.ip[0] = (uint8_t)ip0;
        new_cfg.data.ip[1] = (uint8_t)ip1;
        new_cfg.data.ip[2] = (uint8_t)ip2;
        new_cfg.data.ip[3] = (uint8_t)ip3;
    } else {
        debug_log_with_color(COLOR_RED, "Invalid IP address: %s\n", result.text[3]);
    }

    new_cfg.data.port = (uint16_t)atoi(result.text[4]);

    // Location ID now at text6
    strncpy(new_cfg.data.location_id, result.text[5], sizeof(new_cfg.data.location_id) - 1);

    // Derive seat count from non-empty space names (text7..text10)
    memset(new_cfg.data.space_ids, 0, sizeof(new_cfg.data.space_ids));
    uint8_t seat_count = 0;
    for (uint8_t i = 0; i < SEATSURFING_MAX_SEATS; i++) {
        if (result.text[6 + i][0]) {
            strncpy(new_cfg.data.space_ids[i], result.text[6 + i],
                    sizeof(new_cfg.data.space_ids[i]) - 1);
            seat_count = (uint8_t)(i + 1);
        }
    }
    if (seat_count == 0) {
        // Fallback: keep at least one seat, reuse previous first ID if available
        seat_count = 1;
        strncpy(new_cfg.data.space_ids[0], seatsurfing_config_flash.data.space_ids[0],
                sizeof(new_cfg.data.space_ids[0]) - 1);
    }
    new_cfg.data.seat_count = seat_count;

    bool ok = save_seatsurfing_config(&new_cfg);
    if (ok) {
        debug_log_with_color(COLOR_YELLOW, "Seatsurfing-Konfiguration gespeichert.\n");
    } else {
        debug_log_with_color(COLOR_RED, "Fehler beim Speichern der Seatsurfing-Konfiguration.\n");
    }

    // Keep device-config room settings in sync for SeatSurfing rendering
    device_config_t dev_cfg = {.crc32 = 0};
    memcpy(&dev_cfg.data, &device_config_flash.data, sizeof(device_config_data_t));
    if (result.roomname[0] != '\0') {
        strncpy(dev_cfg.data.roomname, result.roomname, sizeof(dev_cfg.data.roomname) - 1);
    }
    if (strstr(body, "type=") != NULL && result.type >= 0 && result.type <= 2) {
        dev_cfg.data.type = (RoomType)result.type;
    }
    dev_cfg.data.number_of_seats = seat_count;
    if (!save_device_config(&dev_cfg)) {
        debug_log_with_color(COLOR_RED, "Error syncing SeatSurfing-derived room settings.\n");
    }

    send_seatsurfing_config_page(tpcb, "✔ seatsurfing settings stored");
}
#elif defined(USE_CASE_HISTORIAN)
/**
 * @brief Processes Historian API configuration form submissions
 * @param tpcb TCP connection pointer
 * @param body Form data from HTTP POST request
 * @param len Length of form data
 *
 * Parses Historian integration settings and saves configuration to flash memory.
 */
void handle_form_historian(struct tcp_pcb *tpcb, const char *body, size_t len) {
    webserver_set_shutdown_time(make_timeout_time_ms(USER_INTERACTION_TIMEOUT_MS));

    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    web_submission_t result = {0};

    parse_form_fields(body, len, &result);

    historian_config_t new_cfg = {.crc32 = 0};

    // Parse IP address from text1 (format: "192.168.178.42")
    int ip1, ip2, ip3, ip4;
    if (sscanf(result.text[0], "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4) == 4) {
        new_cfg.data.ip[0] = (uint8_t)ip1;
        new_cfg.data.ip[1] = (uint8_t)ip2;
        new_cfg.data.ip[2] = (uint8_t)ip3;
        new_cfg.data.ip[3] = (uint8_t)ip4;
    } else {
        // Default IP on parse error
        new_cfg.data.ip[0] = 192;
        new_cfg.data.ip[1] = 168;
        new_cfg.data.ip[2] = 178;
        new_cfg.data.ip[3] = 42;
    }
    new_cfg.data.port = (uint16_t)atoi(result.text[1]);
    strncpy(new_cfg.data.path, result.text[2], sizeof(new_cfg.data.path) - 1);
    new_cfg.data.datapoint_id = atoi(result.text[3]);
    new_cfg.data.hours_back = atoi(result.text[4]);
    strncpy(new_cfg.data.display_name, result.text[5], sizeof(new_cfg.data.display_name) - 1);

    bool ok = save_historian_config(&new_cfg);
    if (ok) {
        debug_log_with_color(COLOR_YELLOW, "Historian-Konfiguration gespeichert.\n");
    } else {
        debug_log_with_color(COLOR_RED, "Fehler beim Speichern der Historian-Konfiguration.\n");
    }
    send_historian_config_page(tpcb, "✔ historian settings stored");
}
#elif defined(USE_CASE_WEATHERMAP)
void handle_form_weathermap(struct tcp_pcb *tpcb, const char *body, size_t len) {
    webserver_set_shutdown_time(make_timeout_time_ms(USER_INTERACTION_TIMEOUT_MS));

    web_submission_t result = {0};
    parse_form_fields(body, len, &result);

    const char *lat_str = result.text[0];
    const char *lon_str = result.text[1];
    const char *span_str = result.text[2];

    bool valid = true;
    double lat = WEATHERMAP_DEFAULT_CENTER_LAT;
    double lon = WEATHERMAP_DEFAULT_CENTER_LON;
    double half_km = WEATHERMAP_DEFAULT_HALF_WIDTH_M / 1000.0;

    if (lat_str && lat_str[0]) {
        char *endp = NULL;
        lat = strtod(lat_str, &endp);
        if (endp == lat_str || !isfinite(lat))
            valid = false;
    } else {
        valid = false;
    }

    if (lon_str && lon_str[0]) {
        char *endp = NULL;
        lon = strtod(lon_str, &endp);
        if (endp == lon_str || !isfinite(lon))
            valid = false;
    } else {
        valid = false;
    }

    if (span_str && span_str[0]) {
        char *endp = NULL;
        half_km = strtod(span_str, &endp);
        if (endp == span_str || !isfinite(half_km))
            valid = false;
    } else {
        valid = false;
    }

    if (!valid) {
        send_weathermap_page(tpcb,
                             "⚠ Invalid input – please use decimal numbers with '.' as separator.");
        return;
    }

    if (lat < -90.0)
        lat = -90.0;
    if (lat > 90.0)
        lat = 90.0;
    if (lon < -180.0)
        lon = -180.0;
    if (lon > 180.0)
        lon = 180.0;
    if (half_km < 1.0 || half_km > 500.0) {
        send_weathermap_page(tpcb, "⚠ Half width must be between 1 and 500 km.");
        return;
    }

    double half_m = half_km * 1000.0;

    weathermap_config_t cfg;
    if (!load_weathermap_config(&cfg)) {
        cfg = weathermap_config_flash;
    }

    cfg.data.center_lat = lat;
    cfg.data.center_lon = lon;
    cfg.data.half_width_m = half_m;

    bool ok = save_weathermap_config(&cfg);
    if (ok) {
        clear_weathermap_meta();
        debug_log_with_color(COLOR_YELLOW,
                             "[WEATHERMAP] Config saved: lat=%.6f lon=%.6f half_width=%.1fm\n", lat,
                             lon, half_m);
        send_weathermap_page(tpcb, "✔ settings saved. Cached map cleared.");
    } else {
        debug_log_with_color(COLOR_RED, "[WEATHERMAP] Failed to save config\n");
        send_weathermap_page(tpcb, "⚠ Failed to save settings.");
    }
}
#elif defined(USE_CASE_HOMEMATIC)
void handle_form_homematic(struct tcp_pcb *tpcb, const char *body, size_t len) {
    webserver_set_shutdown_time(make_timeout_time_ms(USER_INTERACTION_TIMEOUT_MS));

    web_submission_t result = {0};
    parse_form_fields(body, len, &result);

    homematic_config_t new_cfg = {.crc32 = 0};

    // text1: IP
    int ip1, ip2, ip3, ip4;
    if (sscanf(result.text[0], "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4) == 4) {
        new_cfg.data.ip[0] = (uint8_t)ip1;
        new_cfg.data.ip[1] = (uint8_t)ip2;
        new_cfg.data.ip[2] = (uint8_t)ip3;
        new_cfg.data.ip[3] = (uint8_t)ip4;
    } else {
        // keep previous or set default
        memcpy(new_cfg.data.ip, homematic_config_flash.data.ip, 4);
    }

    // text2: port
    int port = atoi(result.text[1]);
    new_cfg.data.port =
        (port > 0 && port < 65536) ? (uint16_t)port : homematic_config_flash.data.port;

    // Keep original flags in flash (no UI for these now)
    new_cfg.data.add_interface_prefix = homematic_config_flash.data.add_interface_prefix;
    new_cfg.data.auto_label = homematic_config_flash.data.auto_label;

    // Items: for i in 0..HOMEMATIC_MAX_ITEMS-1, fields are text(5+3*i), text(6+3*i), text(7+3*i)
    uint8_t count = 0;
    for (int i = 0; i < HOMEMATIC_MAX_ITEMS; i++) {
        // text5/text6/text7 correspond to zero-based indices 4/5/6
        // Keep original mapping: text5/text6/text7 for row 1 (indices 4/5/6)
        const char *addr = result.text[4 + 3 * i];
        const char *key = result.text[5 + 3 * i];
        const char *lab = result.text[6 + 3 * i];

        if (addr[0] == '\0' && key[0] == '\0' && lab[0] == '\0') {
            continue;
        }
        strncpy(new_cfg.data.items[count].address, addr,
                sizeof(new_cfg.data.items[count].address) - 1);
        strncpy(new_cfg.data.items[count].key, key, sizeof(new_cfg.data.items[count].key) - 1);
        strncpy(new_cfg.data.items[count].fallback_label, lab,
                sizeof(new_cfg.data.items[count].fallback_label) - 1);
        count++;
    }
    new_cfg.data.count = count;

    bool ok = save_homematic_config(&new_cfg);
    send_homematic_config_page(tpcb,
                               ok ? "✔ homematic settings stored" : "⚠ error saving settings");
}
#endif

/**
 * @brief Processes device configuration form submissions
 * @param tpcb TCP connection pointer
 * @param body Form data from HTTP POST request
 * @param len Length of form data
 *
 * Parses device settings including room name, refresh intervals,
 * power management settings, and saves to flash memory.
 */
void handle_form_device_config(struct tcp_pcb *tpcb, const char *body, size_t len) {
    web_submission_t result = {0};

    parse_form_fields(body, len, &result);

    device_config_t new_cfg = {.crc32 = 0}; // Important: struct contains .data + .crc32
    wifi_config_t new_wifi = {.crc32 = 0};

    // Take over existing values
    memcpy(&new_cfg.data, &device_config_flash.data, sizeof(device_config_data_t));
    memcpy(&new_wifi, &wifi_config_flash, sizeof(wifi_config_t));

    // Neue Werte eintragen
#ifndef USE_CASE_SEATSURFING
    strncpy(new_cfg.data.roomname, result.roomname, sizeof(new_cfg.data.roomname) - 1);
#endif
    new_cfg.data.epapertype = (EpaperType)result.epapertype;

    for (int i = 0; i < 8; i++) {
        new_cfg.data.refresh_minutes_by_pushbutton[i] = result.refresh_minutes_by_pushbutton[i];
    }

    new_cfg.data.show_query_date = result.show_query_date;
    new_cfg.data.query_only_at_officehours = result.query_only_at_officehours;
    new_cfg.data.wifi_reconnect_minutes = result.wifi_reconnect_minutes;
    new_cfg.data.watchdog_time = result.watchdog_time;
    new_cfg.data.number_wifi_attempts = result.number_wifi_attempts;
    new_cfg.data.wifi_timeout = result.wifi_timeout;
    new_cfg.data.max_wait_data_wifi = result.max_wait_data_wifi;
    new_cfg.data.conversion_factor = result.conversion_factor;
    new_cfg.data.telemetry_enabled = result.telemetry_enabled;
    new_cfg.data.telemetry_timeout_ms =
        (result.telemetry_timeout_ms >= 100 && result.telemetry_timeout_ms <= 10000)
            ? result.telemetry_timeout_ms
            : 2000;
    strncpy(new_cfg.data.telemetry_host, result.telemetry_host,
            sizeof(new_cfg.data.telemetry_host) - 1);
    new_cfg.data.telemetry_host[sizeof(new_cfg.data.telemetry_host) - 1] = '\0';
    new_cfg.data.telemetry_port = (result.telemetry_port >= 1 && result.telemetry_port <= 65535)
                                      ? (uint16_t)result.telemetry_port
                                      : 3004;
    strncpy(new_cfg.data.telemetry_token, result.telemetry_token,
            sizeof(new_cfg.data.telemetry_token) - 1);
    new_cfg.data.telemetry_token[sizeof(new_cfg.data.telemetry_token) - 1] = '\0';
    strncpy(new_cfg.data.telemetry_label, result.telemetry_label,
            sizeof(new_cfg.data.telemetry_label) - 1);
    new_cfg.data.telemetry_label[sizeof(new_cfg.data.telemetry_label) - 1] = '\0';

    if (result.wifi_ssid[0] != '\0' || result.wifi_password[0] != '\0') {
        strncpy(new_wifi.ssid, result.wifi_ssid, sizeof(new_wifi.ssid) - 1);
        new_wifi.ssid[sizeof(new_wifi.ssid) - 1] = '\0';
        strncpy(new_wifi.password, result.wifi_password, sizeof(new_wifi.password) - 1);
        new_wifi.password[sizeof(new_wifi.password) - 1] = '\0';
    }

    bool ok_wifi = save_wifi_config(&new_wifi);
    bool ok_dev = save_device_config(&new_cfg);

    const char *msg = NULL;
    if (ok_wifi && ok_dev) {
        msg = "✔ Device + WiFi settings saved";
    } else if (!ok_dev && !ok_wifi) {
        msg = "⚠ Error saving Device + WiFi settings";
    } else if (!ok_dev) {
        msg = "⚠ WiFi saved, Device settings failed";
    } else {
        msg = "⚠ Device settings saved, WiFi failed";
    }
    send_device_config_page(tpcb, msg);

    if (ok_dev && ok_wifi) {
        debug_log_with_color(COLOR_GREEN, "Device and WiFi configuration saved\n");
    } else {
        if (!ok_dev) {
            debug_log_with_color(COLOR_RED, "Error saving device configuration\n");
        }
        if (!ok_wifi) {
            debug_log_with_color(COLOR_RED, "Error saving WiFi configuration\n");
        }
    }
}

/**
 * @brief Processes RTC clock setting form submissions
 * @param tpcb TCP connection pointer
 * @param body Form data from HTTP POST request
 * @param len Length of form data
 *
 * Parses time/date values from form and updates the DS3231 RTC.
 * Validates input and provides feedback on setting success/failure.
 */
void handle_form_clock(struct tcp_pcb *tpcb, const char *body, size_t len) {
    const char *line_param = strstr(body, "line=");
    if (!line_param) {
        debug_log_with_color(COLOR_RED, "POST /clock: Kein line= Parameter\n");
        send_clock_page(tpcb, "❌ Kein line= Parameter.");
        return;
    }

    char raw_line[128] = {0};
    char decoded_line[128] = {0};
    sscanf(line_param + 5, "%127[^&\r\n]", raw_line); // robust
    url_decode(decoded_line, raw_line, sizeof(decoded_line));

    debug_log("POST /clock: line = ");
    debug_log(decoded_line);
    debug_log("\n");

    extern ds3231_t ds3231;
    ds3231_data_t old_time, new_time;

    ds3231_read_current_time(&ds3231, &old_time);
    set_rtc_from_display_string(&ds3231, decoded_line);
    ds3231_read_current_time(&ds3231, &new_time);

    int old_min = old_time.hours * 60 + old_time.minutes;
    int new_min = new_time.hours * 60 + new_time.minutes;
    int delta = new_min - old_min;

    char msg[256];
    snprintf(msg, sizeof(msg),
             "✔️ Uhrzeit gesetzt<br>"
             "Vorher: %02d:%02d&nbsp;am&nbsp;%02d.%02d.%04d<br>"
             "Jetzt: %02d:%02d&nbsp;am&nbsp;%02d.%02d.%04d<br>"
             "Differenz: <b>%d&nbsp;Minute%s</b>",
             old_time.hours, old_time.minutes, old_time.date, old_time.month, old_time.year + 2000,
             new_time.hours, new_time.minutes, new_time.date, new_time.month, new_time.year + 2000,
             abs(delta), abs(delta) == 1 ? "" : "n");

    send_clock_page(tpcb, msg);
}

void handle_form_settings_import(struct tcp_pcb *tpcb, const char *body, size_t len) {
    static const char *const wifi_keys[] = {"wifi.ssid", "wifi.password"};
    static const char *const device_keys[] = {
        "device.roomname",
        "device.type",
        "device.epapertype",
        "device.refresh0",
        "device.refresh1",
        "device.refresh2",
        "device.refresh3",
        "device.refresh4",
        "device.refresh5",
        "device.refresh6",
        "device.refresh7",
        "device.show_query_date",
        "device.query_only_at_officehours",
        "device.wifi_reconnect_minutes",
        "device.watchdog_time",
        "device.number_wifi_attempts",
        "device.wifi_timeout",
        "device.max_wait_data_wifi",
        "device.switch_off_battery_voltage",
        "device.conversion_factor",
        "device.telemetry_enabled",
        "device.telemetry_host",
        "device.telemetry_port",
        "device.telemetry_token",
        "device.telemetry_timeout_ms",
        "device.telemetry_label",
    };
    enum {
        WIFI_KEY_COUNT = (int)(sizeof(wifi_keys) / sizeof(wifi_keys[0])),
        DEVICE_KEY_COUNT = (int)(sizeof(device_keys) / sizeof(device_keys[0])),
        MAX_USE_KEYS = 32,
        MAX_UNKNOWN_KEYS = 16
    };

    char decoded[4096];
    if (!extract_urlencoded_field(body, len, "settings", decoded, sizeof(decoded))) {
        send_settings_transfer_page(tpcb, "❌ Missing settings field.");
        return;
    }

    wifi_config_t new_wifi = wifi_config_flash;
    device_config_t new_dev = device_config_flash;
#if defined(USE_CASE_SEATSURFING)
    seatsurfing_config_t new_use = seatsurfing_config_flash;
#elif defined(USE_CASE_HISTORIAN)
    historian_config_t new_use = historian_config_flash;
#elif defined(USE_CASE_HOMEMATIC)
    homematic_config_t new_use = homematic_config_flash;
#endif

    const char *use_keys[MAX_USE_KEYS] = {0};
    size_t use_key_count = 0;
#if defined(USE_CASE_SEATSURFING)
    static const char *const use_keys_seatsurfing[] = {
        "seatsurfing.host",       "seatsurfing.username", "seatsurfing.password",
        "seatsurfing.ip",         "seatsurfing.port",     "seatsurfing.location_id",
        "seatsurfing.seat_count", "seatsurfing.space1",   "seatsurfing.space2",
        "seatsurfing.space3",     "seatsurfing.space4",
    };
    use_key_count = sizeof(use_keys_seatsurfing) / sizeof(use_keys_seatsurfing[0]);
    for (size_t i = 0; i < use_key_count; i++) {
        use_keys[i] = use_keys_seatsurfing[i];
    }
#elif defined(USE_CASE_HISTORIAN)
    static const char *const use_keys_historian[] = {
        "historian.ip",           "historian.port",       "historian.path",
        "historian.datapoint_id", "historian.hours_back", "historian.display_name",
    };
    use_key_count = sizeof(use_keys_historian) / sizeof(use_keys_historian[0]);
    for (size_t i = 0; i < use_key_count; i++) {
        use_keys[i] = use_keys_historian[i];
    }
#elif defined(USE_CASE_HOMEMATIC)
    char use_key_buf[HOMEMATIC_MAX_ITEMS * 3][40];
    use_keys[0] = "homematic.ip";
    use_keys[1] = "homematic.port";
    use_keys[2] = "homematic.count";
    use_key_count = 3;
    for (int i = 0; i < HOMEMATIC_MAX_ITEMS; i++) {
        snprintf(use_key_buf[i * 3 + 0], sizeof(use_key_buf[i * 3 + 0]), "homematic.item%d.address",
                 i + 1);
        snprintf(use_key_buf[i * 3 + 1], sizeof(use_key_buf[i * 3 + 1]), "homematic.item%d.key",
                 i + 1);
        snprintf(use_key_buf[i * 3 + 2], sizeof(use_key_buf[i * 3 + 2]), "homematic.item%d.label",
                 i + 1);
        use_keys[use_key_count++] = use_key_buf[i * 3 + 0];
        use_keys[use_key_count++] = use_key_buf[i * 3 + 1];
        use_keys[use_key_count++] = use_key_buf[i * 3 + 2];
    }
#endif

    bool wifi_seen[WIFI_KEY_COUNT];
    bool wifi_issues[WIFI_KEY_COUNT];
    bool device_seen[DEVICE_KEY_COUNT];
    bool device_issues[DEVICE_KEY_COUNT];
    bool use_seen[MAX_USE_KEYS];
    bool use_issues[MAX_USE_KEYS];
    memset(wifi_seen, 0, sizeof(wifi_seen));
    memset(wifi_issues, 0, sizeof(wifi_issues));
    memset(device_seen, 0, sizeof(device_seen));
    memset(device_issues, 0, sizeof(device_issues));
    memset(use_seen, 0, sizeof(use_seen));
    memset(use_issues, 0, sizeof(use_issues));

    bool touched_wifi = false;
    bool touched_device = false;
    bool touched_use = false;
    char unknown_keys[MAX_UNKNOWN_KEYS][48];
    int unknown_key_total = 0;
    int unknown_key_stored = 0;

    bool use_case_seen = false;
    bool use_case_match = false;

    char *saveptr = NULL;
    char *line = strtok_r(decoded, "\n", &saveptr);
    while (line) {
        size_t llen = strlen(line);
        if (llen > 0 && line[llen - 1] == '\r') {
            line[llen - 1] = '\0';
        }
        if (line[0] == '\0' || line[0] == '#') {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }
        if (strcmp(line, "INKI_SETTINGS_V1") == 0 || strncmp(line, "inki_settings_v", 15) == 0) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if (strcmp(key, "use_case") == 0) {
            use_case_seen = true;
            use_case_match = (strcmp(val, current_use_case_name()) == 0);
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        if (is_import_metadata_key(key)) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        if (strncmp(key, "wifi.", 5) == 0) {
            touched_wifi = true;
        }
        if (strncmp(key, "device.", 7) == 0) {
            touched_device = true;
        }
#if defined(USE_CASE_SEATSURFING)
        if (strncmp(key, "seatsurfing.", 12) == 0) {
            touched_use = true;
        }
#elif defined(USE_CASE_HISTORIAN)
        if (strncmp(key, "historian.", 10) == 0) {
            touched_use = true;
        }
#elif defined(USE_CASE_HOMEMATIC)
        if (strncmp(key, "homematic.", 10) == 0) {
            touched_use = true;
        }
#endif

        int wifi_idx = find_key_index(key, wifi_keys, WIFI_KEY_COUNT);
        int device_idx = find_key_index(key, device_keys, DEVICE_KEY_COUNT);
        int use_idx = find_key_index(key, use_keys, use_key_count);

        bool known_key = (wifi_idx >= 0 || device_idx >= 0 || use_idx >= 0);
        bool key_issue = false;

        if (strcmp(key, "wifi.ssid") == 0) {
            strncpy(new_wifi.ssid, val, sizeof(new_wifi.ssid) - 1);
            new_wifi.ssid[sizeof(new_wifi.ssid) - 1] = '\0';
        } else if (strcmp(key, "wifi.password") == 0) {
            strncpy(new_wifi.password, val, sizeof(new_wifi.password) - 1);
            new_wifi.password[sizeof(new_wifi.password) - 1] = '\0';
        } else if (strcmp(key, "device.roomname") == 0) {
            strncpy(new_dev.data.roomname, val, sizeof(new_dev.data.roomname) - 1);
            new_dev.data.roomname[sizeof(new_dev.data.roomname) - 1] = '\0';
        } else if (strcmp(key, "device.type") == 0) {
            new_dev.data.type = (RoomType)atoi(val);
        } else if (strcmp(key, "device.epapertype") == 0) {
            new_dev.data.epapertype = (EpaperType)atoi(val);
        } else if (strncmp(key, "device.refresh", 14) == 0 && key[14] >= '0' && key[14] <= '7' &&
                   key[15] == '\0') {
            int idx = key[14] - '0';
            new_dev.data.refresh_minutes_by_pushbutton[idx] = atoi(val);
        } else if (strcmp(key, "device.show_query_date") == 0) {
            bool b;
            if (parse_bool_value(val, &b)) {
                new_dev.data.show_query_date = b;
            } else {
                key_issue = true;
            }
        } else if (strcmp(key, "device.query_only_at_officehours") == 0) {
            bool b;
            if (parse_bool_value(val, &b)) {
                new_dev.data.query_only_at_officehours = b;
            } else {
                key_issue = true;
            }
        } else if (strcmp(key, "device.wifi_reconnect_minutes") == 0) {
            new_dev.data.wifi_reconnect_minutes = atoi(val);
        } else if (strcmp(key, "device.watchdog_time") == 0) {
            new_dev.data.watchdog_time = atoi(val);
        } else if (strcmp(key, "device.number_wifi_attempts") == 0) {
            new_dev.data.number_wifi_attempts = atoi(val);
        } else if (strcmp(key, "device.wifi_timeout") == 0) {
            new_dev.data.wifi_timeout = atoi(val);
        } else if (strcmp(key, "device.max_wait_data_wifi") == 0) {
            new_dev.data.max_wait_data_wifi = atoi(val);
        } else if (strcmp(key, "device.switch_off_battery_voltage") == 0) {
            new_dev.data.switch_off_battery_voltage = strtof(val, NULL);
        } else if (strcmp(key, "device.conversion_factor") == 0) {
            new_dev.data.conversion_factor = strtof(val, NULL);
        } else if (strcmp(key, "device.telemetry_enabled") == 0) {
            bool b;
            if (parse_bool_value(val, &b)) {
                new_dev.data.telemetry_enabled = b;
            } else {
                key_issue = true;
            }
        } else if (strcmp(key, "device.telemetry_host") == 0) {
            strncpy(new_dev.data.telemetry_host, val, sizeof(new_dev.data.telemetry_host) - 1);
            new_dev.data.telemetry_host[sizeof(new_dev.data.telemetry_host) - 1] = '\0';
        } else if (strcmp(key, "device.telemetry_port") == 0) {
            new_dev.data.telemetry_port = (uint16_t)atoi(val);
        } else if (strcmp(key, "device.telemetry_token") == 0) {
            strncpy(new_dev.data.telemetry_token, val, sizeof(new_dev.data.telemetry_token) - 1);
            new_dev.data.telemetry_token[sizeof(new_dev.data.telemetry_token) - 1] = '\0';
        } else if (strcmp(key, "device.telemetry_timeout_ms") == 0) {
            new_dev.data.telemetry_timeout_ms = atoi(val);
        } else if (strcmp(key, "device.telemetry_label") == 0) {
            strncpy(new_dev.data.telemetry_label, val, sizeof(new_dev.data.telemetry_label) - 1);
            new_dev.data.telemetry_label[sizeof(new_dev.data.telemetry_label) - 1] = '\0';
#if defined(USE_CASE_SEATSURFING)
        } else if (strcmp(key, "seatsurfing.host") == 0) {
            strncpy(new_use.data.host, val, sizeof(new_use.data.host) - 1);
            new_use.data.host[sizeof(new_use.data.host) - 1] = '\0';
        } else if (strcmp(key, "seatsurfing.username") == 0) {
            strncpy(new_use.data.username, val, sizeof(new_use.data.username) - 1);
            new_use.data.username[sizeof(new_use.data.username) - 1] = '\0';
        } else if (strcmp(key, "seatsurfing.password") == 0) {
            strncpy(new_use.data.password, val, sizeof(new_use.data.password) - 1);
            new_use.data.password[sizeof(new_use.data.password) - 1] = '\0';
        } else if (strcmp(key, "seatsurfing.ip") == 0) {
            if (!parse_ipv4_value(val, new_use.data.ip)) {
                key_issue = true;
            }
        } else if (strcmp(key, "seatsurfing.port") == 0) {
            new_use.data.port = (uint16_t)atoi(val);
        } else if (strcmp(key, "seatsurfing.location_id") == 0) {
            strncpy(new_use.data.location_id, val, sizeof(new_use.data.location_id) - 1);
            new_use.data.location_id[sizeof(new_use.data.location_id) - 1] = '\0';
        } else if (strcmp(key, "seatsurfing.seat_count") == 0) {
            new_use.data.seat_count = (uint8_t)atoi(val);
        } else if (strcmp(key, "seatsurfing.space1") == 0) {
            strncpy(new_use.data.space_ids[0], val, sizeof(new_use.data.space_ids[0]) - 1);
            new_use.data.space_ids[0][sizeof(new_use.data.space_ids[0]) - 1] = '\0';
        } else if (strcmp(key, "seatsurfing.space2") == 0) {
            strncpy(new_use.data.space_ids[1], val, sizeof(new_use.data.space_ids[1]) - 1);
            new_use.data.space_ids[1][sizeof(new_use.data.space_ids[1]) - 1] = '\0';
        } else if (strcmp(key, "seatsurfing.space3") == 0) {
            strncpy(new_use.data.space_ids[2], val, sizeof(new_use.data.space_ids[2]) - 1);
            new_use.data.space_ids[2][sizeof(new_use.data.space_ids[2]) - 1] = '\0';
        } else if (strcmp(key, "seatsurfing.space4") == 0) {
            strncpy(new_use.data.space_ids[3], val, sizeof(new_use.data.space_ids[3]) - 1);
            new_use.data.space_ids[3][sizeof(new_use.data.space_ids[3]) - 1] = '\0';
#elif defined(USE_CASE_HISTORIAN)
        } else if (strcmp(key, "historian.ip") == 0) {
            if (!parse_ipv4_value(val, new_use.data.ip)) {
                key_issue = true;
            }
        } else if (strcmp(key, "historian.port") == 0) {
            new_use.data.port = (uint16_t)atoi(val);
        } else if (strcmp(key, "historian.path") == 0) {
            strncpy(new_use.data.path, val, sizeof(new_use.data.path) - 1);
            new_use.data.path[sizeof(new_use.data.path) - 1] = '\0';
        } else if (strcmp(key, "historian.datapoint_id") == 0) {
            new_use.data.datapoint_id = atoi(val);
        } else if (strcmp(key, "historian.hours_back") == 0) {
            new_use.data.hours_back = atoi(val);
        } else if (strcmp(key, "historian.display_name") == 0) {
            strncpy(new_use.data.display_name, val, sizeof(new_use.data.display_name) - 1);
            new_use.data.display_name[sizeof(new_use.data.display_name) - 1] = '\0';
#elif defined(USE_CASE_HOMEMATIC)
        } else if (strcmp(key, "homematic.ip") == 0) {
            if (!parse_ipv4_value(val, new_use.data.ip)) {
                key_issue = true;
            }
        } else if (strcmp(key, "homematic.port") == 0) {
            new_use.data.port = (uint16_t)atoi(val);
        } else if (strcmp(key, "homematic.count") == 0) {
            new_use.data.count = (uint8_t)atoi(val);
        } else if (strncmp(key, "homematic.item", 13) == 0) {
            char *num_end = NULL;
            long item_num = strtol(key + 13, &num_end, 10);
            int idx = (int)item_num - 1;
            const char *suffix = num_end;
            if (idx >= 0 && idx < HOMEMATIC_MAX_ITEMS && suffix && suffix[0] == '.') {
                if (strcmp(suffix, ".address") == 0) {
                    strncpy(new_use.data.items[idx].address, val,
                            sizeof(new_use.data.items[idx].address) - 1);
                    new_use.data.items[idx].address[sizeof(new_use.data.items[idx].address) - 1] =
                        '\0';
                } else if (strcmp(suffix, ".key") == 0) {
                    strncpy(new_use.data.items[idx].key, val,
                            sizeof(new_use.data.items[idx].key) - 1);
                    new_use.data.items[idx].key[sizeof(new_use.data.items[idx].key) - 1] = '\0';
                } else if (strcmp(suffix, ".label") == 0) {
                    strncpy(new_use.data.items[idx].fallback_label, val,
                            sizeof(new_use.data.items[idx].fallback_label) - 1);
                    new_use.data.items[idx]
                        .fallback_label[sizeof(new_use.data.items[idx].fallback_label) - 1] = '\0';
                } else {
                    key_issue = true;
                }
            } else {
                key_issue = true;
            }
#endif
        }

        if (known_key) {
            if (wifi_idx >= 0) {
                wifi_seen[wifi_idx] = true;
                if (key_issue) {
                    wifi_issues[wifi_idx] = true;
                }
            } else if (device_idx >= 0) {
                device_seen[device_idx] = true;
                if (key_issue) {
                    device_issues[device_idx] = true;
                }
            } else if (use_idx >= 0) {
                use_seen[use_idx] = true;
                if (key_issue) {
                    use_issues[use_idx] = true;
                }
            }
        } else {
            unknown_key_total++;
            if (unknown_key_stored < MAX_UNKNOWN_KEYS) {
                strncpy(unknown_keys[unknown_key_stored], key, sizeof(unknown_keys[0]) - 1);
                unknown_keys[unknown_key_stored][sizeof(unknown_keys[0]) - 1] = '\0';
                unknown_key_stored++;
            }
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    if (!use_case_seen) {
        send_settings_transfer_page(tpcb, "❌ Missing use_case in import file.");
        return;
    }

    if (!use_case_match) {
        send_settings_transfer_page(tpcb, "❌ use_case does not match current firmware.");
        return;
    }

    int imported_count = 0;
    int issue_count = 0;
    int kept_old_count = 0;

    char *report_html = malloc(4096);
    if (report_html) {
        size_t report_off = 0;
        report_html[0] = '\0';
        append_htmlf(report_html, 4096, &report_off, "<div class='report'>");
        append_htmlf(report_html, 4096, &report_off, "<h2>Import details</h2>");
        append_key_report_section(report_html, 4096, &report_off, "Wi-Fi keys", wifi_keys,
                                  WIFI_KEY_COUNT, wifi_seen, wifi_issues, touched_wifi,
                                  &imported_count, &issue_count, &kept_old_count);
        append_key_report_section(report_html, 4096, &report_off, "Device keys", device_keys,
                                  DEVICE_KEY_COUNT, device_seen, device_issues, touched_device,
                                  &imported_count, &issue_count, &kept_old_count);
        append_key_report_section(report_html, 4096, &report_off, "Use-case keys", use_keys,
                                  use_key_count, use_seen, use_issues, touched_use, &imported_count,
                                  &issue_count, &kept_old_count);
        if (unknown_key_stored > 0) {
            append_htmlf(report_html, 4096, &report_off, "<h3>Unknown keys</h3><ul>");
            for (int i = 0; i < unknown_key_stored; i++) {
                append_htmlf(report_html, 4096, &report_off,
                             "<li class='status-old'><code>%s</code> ignored</li>",
                             unknown_keys[i]);
            }
            if (unknown_key_total > unknown_key_stored) {
                append_htmlf(report_html, 4096, &report_off,
                             "<li class='status-old'>... and %d more unknown keys</li>",
                             unknown_key_total - unknown_key_stored);
            }
            append_htmlf(report_html, 4096, &report_off, "</ul>");
        }
        append_htmlf(report_html, 4096, &report_off,
                     "<div class='report-summary'>Imported: <b>%d</b> | Issues: <b>%d</b> | Kept "
                     "old: <b>%d</b> | Unknown ignored: <b>%d</b></div>",
                     imported_count, issue_count, kept_old_count, unknown_key_total);
        append_htmlf(report_html, 4096, &report_off, "</div>");
    } else {
        // Fallback for low-memory case: still compute issue_count for result severity.
        for (int i = 0; i < WIFI_KEY_COUNT; i++) {
            if (wifi_seen[i]) {
                if (wifi_issues[i]) {
                    issue_count++;
                }
            } else if (touched_wifi) {
                issue_count++;
            }
        }
        for (int i = 0; i < DEVICE_KEY_COUNT; i++) {
            if (device_seen[i]) {
                if (device_issues[i]) {
                    issue_count++;
                }
            } else if (touched_device) {
                issue_count++;
            }
        }
        for (size_t i = 0; i < use_key_count; i++) {
            if (use_seen[i]) {
                if (use_issues[i]) {
                    issue_count++;
                }
            } else if (touched_use) {
                issue_count++;
            }
        }
    }

    bool ok_wifi = save_wifi_config(&new_wifi);
    bool ok_dev = save_device_config(&new_dev);
#if defined(USE_CASE_SEATSURFING)
    bool ok_use = save_seatsurfing_config(&new_use);
#elif defined(USE_CASE_HISTORIAN)
    bool ok_use = save_historian_config(&new_use);
#elif defined(USE_CASE_HOMEMATIC)
    bool ok_use = save_homematic_config(&new_use);
#else
    bool ok_use = true;
#endif

    if (ok_wifi && ok_dev && ok_use) {
        if (issue_count > 0) {
            send_settings_transfer_page_with_report(tpcb, "⚠ Settings imported with issues.",
                                                    report_html ? report_html : "");
        } else {
            send_settings_transfer_page_with_report(tpcb, "✔ Settings imported and saved.",
                                                    report_html ? report_html : "");
        }
    } else {
        send_settings_transfer_page_with_report(tpcb, "❌ Error while saving imported settings.",
                                                report_html ? report_html : "");
    }

    if (report_html) {
        free(report_html);
    }
}
