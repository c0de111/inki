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
#include "hardware/sync.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "webserver.h"
#include "lwip/tcp.h"
#include <string.h>
#include <stdio.h>
#include "pico/time.h"
#include "debug.h"
#include "main.h"
#include "flash.h"
#include "wifi.h"
#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "ds3231.h"
#include "webserver_utils.h"
#include "config.h"
#include "GUI_Paint.h"
#include "morse.h"

// =============================================================================
// HTML PAGE GENERATION FUNCTIONS
// =============================================================================

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
             "a { display: inline-block; padding: 0.6em 1em; font-size: 1em; margin: 0.5em; width: 80%%; max-width: 200px; background: #eee; border: 1px solid #ccc; border-radius: 5px; text-decoration: none; color: black; }"
             "a:hover { background: #ddd; }"
             "p { margin-top: 2em; font-size: 0.9em; }"
             "</style></head><body>\n");

    strcat(page,
           "<h1>inki Setup</h1>"
           "<a href=\"/wifi\">Wi-Fi Settings</a><br>"
#ifdef USE_CASE_SEATSURFING
           "<a href=\"/seatsurfing\">Seatsurfing Settings</a><br>"
#elif defined(USE_CASE_HISTORIAN)
           "<a href=\"/historian\">Historian Settings</a><br>"
#endif
           "<a href=\"/device_settings\">Device Settings</a><br>"
           "<a href=\"/upload_logo\">Upload Logo</a><br>"
           "<a href=\"/device_status\">Device Status</a><br>"
           "<a href=\"/message\">Your Custom Message</a><br>"
           "<a href=\"/firmware_update\">Firmware Update</a><br>"
           "<a href=\"/clock\">Set Clock</a><br>"
           "<a href=\"/shutdown\">Reboot</a>");
/*
    strcat(page,
           "<img src=\"/logo\" alt=\"inki logo\" width=\"104\" height=\"95\"><br>");*/

    snprintf(page + strlen(page), sizeof(page) - strlen(page),
             "<p>%s</p></body></html>", timeout_info);
    debug_log("Landing page length: %d\n", strlen(page));

    send_response(tpcb, page);
}

void send_message_page(struct tcp_pcb* tpcb, const char* message) {
    char page[4096];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    const char* info = (message && *message) ? message : "";

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
        snprintf(page + strlen(page), sizeof(page) - strlen(page),
                 "<p style='color:green'>%s</p>", info);
    }

    bool can_print = (device_config_flash.data.epapertype != EPAPER_NONE);
    snprintf(page + strlen(page), sizeof(page) - strlen(page),
           "<form method=\"POST\" action=\"/message\">"
           "<label for=msg>Message</label>"
           "<textarea id=msg name=\"text1\" rows=4 maxlength=240 placeholder=\"Type your message\"></textarea>"
           "<fieldset><legend>Action</legend>"
           "<label><input type=\"radio\" name=\"action\" value=\"morse\" checked> Morse on LED</label><br>"
           "<div style=\"padding-left:1em\">"
           "<label for=unit>Unit (ms)</label>"
           "<input id=unit name=\"unit_ms\" type=\"number\" min=\"50\" max=\"2000\" step=\"10\" value=\"%d\">"
           "</div>"
           "<label><input type=\"radio\" name=\"action\" value=\"print\" %s> Print on ePaper & Power Down</label>"
           "<div style=\"padding-left:1em\">"
           "<label for=font>Font size</label>"
           "<select id=font name=\"font_size\"><option>small</option><option selected>medium</option><option>large</option></select>"
           "<label for=align>Alignment</label>"
           "<select id=align name=\"align\"><option>left</option><option selected>center</option></select>"
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
           LED_MORSE_UNIT_MS,
           can_print ? "" : "disabled");

    snprintf(page + strlen(page), sizeof(page) - strlen(page),
             "<p>%s</p></body></html>", timeout_info);

    send_response(tpcb, page);
}

static const sFONT* pick_font(const char* size) {
    if (!size) return &font_ubuntu_mono_14pt;
    if (strncmp(size, "large", 5) == 0) return &font_ubuntu_mono_18pt_bold;
    if (strncmp(size, "small", 5) == 0) return &font_ubuntu_mono_10pt;
    return &font_ubuntu_mono_14pt;
}

static void draw_wrapped_text(UBYTE* img, int width, int height, const char* msg, const sFONT* font, bool center) {
    Paint_SelectImage(img);
    int margin = 20;
    int x0 = margin;
    int y = margin;
    int max_cols = (width - 2*margin) / font->Width;
    int line_h = font->Height + 2;
    if (max_cols < 1) return;

    const char* p = msg;
    char line[256];
    while (*p && y + font->Height <= height - margin) {
        int count = 0;
        const char* line_start = p;
        const char* last_space = NULL;
        while (*p && count < max_cols) {
            if (*p == ' ') last_space = p;
            p++; count++;
        }
        if (*p && last_space && last_space > line_start) {
            // wrap at last space
            int take = last_space - line_start;
            if (take > (int)sizeof(line)-1) take = sizeof(line)-1;
            memcpy(line, line_start, take);
            line[take] = '\0';
            p = last_space + 1;
        } else {
            int take = p - line_start;
            if (take > (int)sizeof(line)-1) take = sizeof(line)-1;
            memcpy(line, line_start, take);
            line[take] = '\0';
        }
        int x = x0;
        if (center) {
            int line_px = strlen(line) * font->Width;
            x = (width - line_px) / 2;
            if (x < margin) x = margin;
        }
        Paint_DrawString_EN(x, y, line, (sFONT*)font, WHITE, BLACK);
        y += line_h;
    }
}

void handle_form_message(struct tcp_pcb *tpcb, const char *body, size_t len) {
    web_submission_t result;
    parse_form_fields(body, len, &result);

    const char* action = (*result.action) ? result.action : "morse";
    const char* msg = result.text[0][0] ? result.text[0] : "INKI";

    if (strncmp(action, "morse", 5) == 0) {
        if (result.aborted) {
            morse_set_enabled(false);
            send_message_page(tpcb, "Morse stopped");
            return;
        }
        int unit = result.unit_ms;
        if (unit >= 50 && unit <= 2000) morse_set_unit_ms(unit);
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

    UBYTE* BlackImage = init_epaper();
    if (!BlackImage) {
        send_message_page(tpcb, "Failed to init ePaper");
        return;
    }

    // Canvas is already selected by init_epaper(); ensure clear
    Paint_Clear(WHITE);

    int width = Paint.Width;
    int height = Paint.Height;
    const sFONT* font = pick_font(result.font_size);
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
void send_device_status_page(struct tcp_pcb* tpcb) {
    char page[4096];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    char buffer[1024];
    char buffer2[1024];
    uint8_t mac_address[6];

    extern ds3231_t ds3231;
    ds3231_data_t now;

    // Get MAC address using public cyw43_state
    if (cyw43_wifi_get_mac(&cyw43_state, 0, mac_address) == 0) {
        // success: mac_address[0]..[5] now valid
    } else {
        // fallback: e.g. clear array or mark as invalid
        memset(mac_address, 0, sizeof(mac_address));
    }

    ds3231_read_current_time(&ds3231, &now);
    float vcc = read_battery_voltage(device_config_flash.data.conversion_factor);
    float vbat = read_coin_cell_voltage(device_config_flash.data.conversion_factor);
    float temp_c = read_onchip_temperature_c();
    float rtc_temp_c = read_ds3231_temperature_c();
    memory_info_t mem = {0};
    get_memory_info(&mem);

    // Voltage assessment (for simple color coding)
    const char* vcc_color = (vcc > 3.5) ? "green" : (vcc > 3.0 ? "orange" : "red");
    const char* vbat_color = (vbat > 3.1) ? "green" : (vbat > 2.9 ? "orange" : "red");

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta charset=\"UTF-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
             "<meta http-equiv=\"refresh\" content=\"300\">"
             "<title>Device Status</title>"
             "<style>"
             "body { font-family: sans-serif; text-align: center; padding: 1em; }"
             ".value { font-weight: bold; }"
             ".green { color: green; }"
             ".orange { color: orange; }"
             ".red { color: red; }"
             ".section { margin-bottom: 1.2em; }"
             "a { display: inline-block; margin-top: 2em; text-decoration: none; color: #0066cc; }"
             "</style></head><body>"
             "<h1>Device Status</h1>");

    // Raumname & SSID
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>Room: <span class='value'>%s</span><br>"
             "SSID: <span class='value'>%s</span></div>",
             device_config_flash.data.roomname,
             wifi_config_flash.ssid);
    strcat(page, buffer);

    // Wi-Fi Einstellungen
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>Reconnect Interval: <span class='value'>%d min</span><br>"
             "Wi-Fi Timeout: <span class='value'>%d s</span></div>",
             device_config_flash.data.wifi_reconnect_minutes,
             device_config_flash.data.wifi_timeout);
    strcat(page, buffer);

    // Refresh-Minuten
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>Refresh Intervals:<br><span class='value'>[%d, %d, %d, %d, %d, %d, %d, %d]</span></div>",
             device_config_flash.data.refresh_minutes_by_pushbutton[0],
             device_config_flash.data.refresh_minutes_by_pushbutton[1],
             device_config_flash.data.refresh_minutes_by_pushbutton[2],
             device_config_flash.data.refresh_minutes_by_pushbutton[3],
             device_config_flash.data.refresh_minutes_by_pushbutton[4],
             device_config_flash.data.refresh_minutes_by_pushbutton[5],
             device_config_flash.data.refresh_minutes_by_pushbutton[6],
             device_config_flash.data.refresh_minutes_by_pushbutton[7]);
    strcat(page, buffer);

    // RTC Zeit (roh)
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>RTC (raw): <span class='value'>%02d:%02d, %s, %02d. %s %04d</span></div>",
             now.hours, now.minutes,
             get_day_of_week(now.day),
             now.date, get_month_name(now.month),
             2000 + now.year);
    strcat(page, buffer);

    // RTC Zeit (DST)
    format_rtc_time(&now, buffer2, sizeof(buffer2));
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>RTC (DST): <span class='value'>%s</span></div>", buffer2);
    strcat(page, buffer);

    // MAC Adresse
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>MAC address: <span class='value'>%02X:%02X:%02X:%02X:%02X:%02X</span></div>",
             mac_address[0], mac_address[1], mac_address[2],
             mac_address[3], mac_address[4], mac_address[5]);
    strcat(page, buffer);

    // Spannungen
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>"
             "Vcc: <span class='value %s'>%.3f V</span><br>"
             "Vbat: <span class='value %s'>%.3f V</span><br>"
             "ADC Conversion Factor: <span class='value'>%.8f</span></div>",
             vcc_color, vcc,
             vbat_color, vbat,
             device_config_flash.data.conversion_factor);
    strcat(page, buffer);

    // On-chip temperature
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>MCU Temperature: <span class='value'>%.1f &deg;C</span></div>",
             temp_c);
    strcat(page, buffer);

    // DS3231 temperature
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>RTC Temperature: <span class='value'>%.1f &deg;C</span></div>",
             rtc_temp_c);
    strcat(page, buffer);

    // Memory info
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>Memory:<br>"
             "Heap used: <span class='value'>%u KB</span><br>"
             "Heap headroom: <span class='value'>%u KB</span><br>"
             "Stack margin: <span class='value'>%u KB (approx)</span></div>",
             (unsigned)(mem.heap_used_bytes / 1024U),
             (unsigned)(mem.heap_headroom_bytes / 1024U),
             (unsigned)(mem.stack_margin_bytes / 1024U));
    strcat(page, buffer);

    // Logo-Flash-Info :
    int logo_width = 0, logo_height = 0, logo_size = 0;
    if (get_flash_logo_info(&logo_width, &logo_height, &logo_size)) {
        snprintf(buffer, sizeof(buffer),
                 "<div class='section'>Logo in flash:<br>"
                 "<span class='value'>%dx%d px, %d Bytes</span></div>",
                 logo_width, logo_height, logo_size);
    } else {
        snprintf(buffer, sizeof(buffer),
                 "<div class='section'>Logo in flash: <span class='value red'>not present</span></div>");
    }
    strcat(page, buffer);

// active slot and reset pointer
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>Aktive Firmware:<br>"
             "<div><span class='value'>%s</span></div><br>",
             get_active_firmware_slot_info());

    strcat(page, buffer);

    char build0[16] = {0}, version0[32] = {0};
    char build1[16] = {0}, version1[32] = {0};
    uint32_t size0 = 0, size1 = 0;
    uint32_t crc0 = 0, crc1 = 0;
    uint8_t slot_index0 = 0, slot_index1 = 0;
    uint8_t valid0 = 0, valid1 = 0;

    bool has0 = get_firmware_slot_info(0, build0, version0, &size0, &crc0, &slot_index0, &valid0);
    bool has1 = get_firmware_slot_info(1, build1, version1, &size1, &crc1, &slot_index1, &valid1);

    // Slot 0 HTML
    if (has0) {
        snprintf(buffer, sizeof(buffer),
                 "<div>Slot 0:</div>\n"
                 "<div>Version: <span class='value'>%s</span></div>\n"
                 "<div>Build: <span class='value'>%s</span></div>\n"
                 "<div>Size: <span class='value'>%u Bytes</span></div>\n"
                 "<div>CRC32: <span class='value'>0x%08X</span></div>\n"
                 "<div>Slot: <span class='value'>%u</span></div>\n"
                 "<div>Valid: <span class='value'>%u</span></div><br>\n\n\n",
                 version0, build0, size0, crc0, slot_index0, valid0);
    } else {
        snprintf(buffer, sizeof(buffer),
                 "<li>Slot 0: <span class='value red'>empty or invalid</span></li>\n");
    }
    strcat(page, buffer);

    // Slot 1 HTML
    if (has1) {
        snprintf(buffer, sizeof(buffer),
                 "<div>Slot 1:</div>\n"
                 "<div>Version: <span class='value'>%s</span></div>\n"
                 "<div>Build: <span class='value'>%s</span></div>\n"
                 "<div>Size: <span class='value'>%u Bytes</span></div>\n"
                 "<div>CRC32: <span class='value'>0x%08X</span></div>\n"
                 "<div>Slot: <span class='value'>%u</span></div>\n"
                 "<div>Valid: <span class='value'>%u</span></div>\n",
                 version1, build1, size1, crc1, slot_index1, valid1);
    } else {
        snprintf(buffer, sizeof(buffer),
                 "<li>Slot 1: <span class='value red'>empty or invalid</span></li>\n");
    }
    strcat(page, buffer);

        // Close list
        strcat(page, "</ul></div>\n");

    // Back link
    strcat(page, "<a href=\"/\">back</a></body></html>");

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
void send_upload_logo_page(struct tcp_pcb* tpcb, const char* message) {
    char page[4096];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width, initial-scale=1'>"
             // "<meta http-equiv=\"refresh\" content=\"5\">"
             "<title>Logo Upload</title>"
             "<style>"
             "body { font-family: sans-serif; text-align: center; margin: 2em; }"
             "input[type='file'] { font-size: 1em; padding: 0.5em; margin: 0.5em auto; display: block; width: 80%%; max-width: 300px; }"
             "button { font-size: 1em; padding: 0.5em; margin: 0.5em auto; display: block; width: 80%%; max-width: 300px; }"
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
             "const MAX_SIZE = %d;"  // Wird korrekt ersetzt
             "function upload() {"
             "  const file = document.getElementById('fileInput').files[0];"
             "  if (!file) return;"
             "  if (file.size > MAX_SIZE) {"
             "    document.getElementById('status').innerText = '❌ Datei zu groß (' + file.size + ' Bytes, maximal ' + MAX_SIZE + ' Bytes erlaubt)';"
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
             (message && *message) ? message : "",
             LOGO_FLASH_SIZE
    );

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


    snprintf(page + strlen(page), sizeof(page) - strlen(page),
             "<p>%s</p></body></html>", timeout_info);
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
void send_firmware_update_page(struct tcp_pcb* tpcb, const char* message) {
    // Wenn `message` mit "<div" oder "<h2" beginnt, nur das Fragment senden
    if (message && *message && (
        strncmp(message, "<div", 4) == 0 ||
        strncmp(message, "<h2", 3) == 0)) {
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
        const int duration = upload_session.flash_estimated_duration > 0 ? upload_session.flash_estimated_duration : 15000;

        snprintf(page, sizeof(page),
                 "<!DOCTYPE html><html><head>"
                 "<meta charset=\"UTF-8\">"
                 "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                 "<title>firmware update</title>"
                 "<style>"
                 "body { font-family: sans-serif; text-align: center; margin: 2em; }"
                 "input[type='file'] { font-size: 1em; padding: 0.5em; margin: 0.5em auto; display: block; width: 80%%; max-width: 300px; }"
                 "button { font-size: 1em; padding: 0.5em; margin: 0.5em auto; display: block; width: 80%%; max-width: 300px; }"
                 "#status { margin-top: 1em; font-weight: bold; color: red; }"
                 "progress { width: 80%%; max-width: 300px; height: 2em; margin-top: 1em; }"
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

        strcat(page,
               "<input type='file' id='fileInput'><br>"
               "<button onclick='upload()'>Upload</button><br>"
               "<progress id='progressBar' max='100' value='0'></progress>"
               "<p id='status'></p>"
               "<div id='uploadResult'></div>"
               "<a href='/'>Back</a>\n");

        snprintf(page + strlen(page), sizeof(page) - strlen(page),
                 "<script>"
                 "const MAX_SIZE = %d;"
                 "let interval = null;"
                 "function simulateFlashingProgress(durationMs = %d) {"
                 "  let startTime = Date.now();"
                 "  interval = setInterval(() => {"
                 "    const elapsed = Date.now() - startTime;"
                 "    let percent = Math.min(100, Math.round(elapsed / durationMs * 100));"
                 "    document.getElementById('progressBar').value = percent;"
                 "    document.getElementById('status').innerText = 'Flashen: ' + percent + '%%';"
                 "    if (percent >= 100) {"
                 "      clearInterval(interval);"
                 "      document.getElementById('progressBar').value = 100;"
                 "      document.getElementById('status').innerText = '❌ timeout';"
                 "    }"
                 "  }, 300);"
                 "}"

                 "function upload() {"
                 "  const file = document.getElementById('fileInput').files[0];"
                 "  if (!file) return;"
                 "  if (file.size > MAX_SIZE) {"
                 "    document.getElementById('status').innerText = '❌ File too large (' + file.size + ' bytes, maximum ' + MAX_SIZE + ' bytes allowed)';"
                 "    return;"
                 "  }"
                 "  const xhr = new XMLHttpRequest();"
                 "  xhr.open('POST', '/firmware_update', true);"
                 "  xhr.setRequestHeader('Content-Type', 'application/octet-stream');"
                 "  xhr.responseType = 'text';"
                 "  xhr.onerror = function() {"
                 "    clearInterval(interval);"
                 "    document.getElementById('status').innerText = '❌ Fehler beim Upload';"
                 "  };"
                 "  xhr.onload = function() {"
                 "    clearInterval(interval);"
                 "    document.getElementById('progressBar').value = 100;"
                 "    document.getElementById('status').innerText = '';"
                 "    document.getElementById('uploadResult').innerHTML = xhr.responseText;"
                 "  };"
                 "  xhr.send(file);"
                 "  simulateFlashingProgress();"
                 "}"
                 "</script></body></html>",
                 max_size, duration);

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
                snprintf(page + strlen(page), sizeof(page) - strlen(page),
                         "<div>Slot 0: %s (%s), %u Bytes</div>\n",
                         version0, build0, size0);
            } else {
                strcat(page, "<div>Slot 0: <i>empty or invalid</i></div>\n");
            }

            if (has1) {
                snprintf(page + strlen(page), sizeof(page) - strlen(page),
                         "<div>Slot 1: %s (%s), %u Bytes</div>\n",
                         version1, build1, size1);
            } else {
                strcat(page, "<div>Slot 1: <i>empty or invalid</i></div>\n");
            }

            strcat(page, "</ul>\n");
        } else {
            strcat(page, "<p><i>No valid firmware found in Slot 0 or 1.</i></p>\n");
        }

        snprintf(page + strlen(page), sizeof(page) - strlen(page),
                 "<p>%s</p></body></html>", timeout_info);

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
             "input[type='submit'] { padding: 0.6em 1em; font-size: 1em; margin: 0.5em; width: 45%%; max-width: 150px; }"
             "a { display: inline-block; margin-top: 1.5em; font-size: 0.9em; text-decoration: none; color: #0066cc; }"
             ".message { margin: 1em auto; font-size: 1em; font-weight: bold; color: green; }"
             "</style></head><body>"
             "<h1>Wi-Fi Konfiguration</h1>");

    if (message && *message) {
        snprintf(page + strlen(page), sizeof(page) - strlen(page),
                 "<div class='message'>%s</div>", message);
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
             wifi_config_flash.ssid,
             wifi_config_flash.password,
             timeout_info);

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
void send_seatsurfing_config_page(struct tcp_pcb* tpcb, const char* message) {
    char page[2048];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    char ip_string[16];
    snprintf(ip_string, sizeof(ip_string), "%d.%d.%d.%d",
             seatsurfing_config_flash.data.ip[0],
             seatsurfing_config_flash.data.ip[1],
             seatsurfing_config_flash.data.ip[2],
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
             "input[type='text'] { width: 100%%; padding: 0.5em; font-size: 1em; }"
             "input[type='submit'] { padding: 0.6em 1em; font-size: 1em; margin: 0.5em; width: 45%%; max-width: 150px; }"
             "a { display: inline-block; margin-top: 1.5em; font-size: 0.9em; text-decoration: none; color: #0066cc; }"
             "</style></head><body>"
             "<h1>Seatsurfing Konfiguration</h1>");

            if (message && *message) {
                snprintf(page + strlen(page), sizeof(page) - strlen(page),
                        "<div class='message'>%s</div>", message);
            }

             snprintf(page + strlen(page), sizeof(page) - strlen(page),
             "<form method=\"POST\" action=\"/seatsurfing\">"
             "<label>API Host:<br><input type=\"text\" name=\"text1\" value=\"%s\"></label>"
             "<label>Benutzername:<br><input type=\"text\" name=\"text2\" value=\"%s\"></label>"
             "<label>Passwort:<br><input type=\"text\" name=\"text3\" value=\"%s\"></label>"
             "<label>IP-Adresse:<br><input type=\"text\" name=\"text4\" value=\"%s\"></label>"
             "<label>Port:<br><input type=\"text\" name=\"text5\" value=\"%d\"></label>"
             "<label>Space ID:<br><input type=\"text\" name=\"text6\" value=\"%s\"></label>"
             "<label>Location ID:<br><input type=\"text\" name=\"text7\" value=\"%s\"></label>"
             "<input type=\"submit\" value=\"store\">"
             "</form>"
             "<a href=\"/\">Back to Start</a>"
             "<p>%s</p>"
             "</body></html>",
             seatsurfing_config_flash.data.host,
             seatsurfing_config_flash.data.username,
             seatsurfing_config_flash.data.password,
             ip_string,
             seatsurfing_config_flash.data.port,
             seatsurfing_config_flash.data.space_id,
             seatsurfing_config_flash.data.location_id,
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
void send_historian_config_page(struct tcp_pcb* tpcb, const char* message) {
    char page[2048];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    snprintf(page, sizeof(page),
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
             "input[type='submit'] { padding: 0.6em 1em; font-size: 1em; margin: 0.5em; width: 45%%; max-width: 150px; }"
             "a { display: inline-block; margin-top: 1.5em; font-size: 0.9em; text-decoration: none; color: #0066cc; }"
             "</style></head><body>"
             "<h1>Historian Configuration</h1>");

            if (message && *message) {
                snprintf(page + strlen(page), sizeof(page) - strlen(page),
                        "<div class='message'>%s</div>", message);
            }

             snprintf(page + strlen(page), sizeof(page) - strlen(page),
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
             historian_config_flash.data.port,
             historian_config_flash.data.path,
             historian_config_flash.data.datapoint_id,
             historian_config_flash.data.hours_back,
             historian_config_flash.data.display_name,
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

    // Zeit als Rohwert (RTC pur)
    char current_raw[64];
    snprintf(current_raw, sizeof(current_raw), "%02d:%02d:%02d %02d.%02d.%04d",
             current.hours, current.minutes, current.seconds,
             current.date, current.month, current.year + 2000);

    // Zeit inkl. Sommerzeit
    char current_dst[64];
    format_rtc_time(&current, current_dst, sizeof(current_dst));

    snprintf(page, sizeof(page),
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
             "<p id=\"preview\">Lokale Zeit wird ermittelt…</p>"
             "<input type=\"submit\" value=\"Uhr stellen\">"
             "</form>"

             "<p><a href=\"/\">Back</a></p>"
             "<p>%s</p>"

             "<script>"
             "const now = new Date();"
             "const weekday = ['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'][now.getDay()];"
             "const months = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];"
             "const day = now.getDate();"
             "const month = months[now.getMonth()];"
             "const year = now.getFullYear();"
             "const hour = now.getHours().toString().padStart(2,'0');"
             "const minute = now.getMinutes().toString().padStart(2,'0');"
             "const line = `${weekday}, ${day}. ${month} ${year}, ${hour}:${minute}`;"
             "document.getElementById('line').value = line;"
             "document.getElementById('preview').textContent = 'Lokale Zeit: ' + line;"
             "</script>"
             "</body></html>",
             current_raw,
             current_dst,
             (message && *message) ? "<div class='message'>" : "",
             (message && *message) ? message : "",
             (message && *message) ? "</div>" : "",
             timeout_info);

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
void send_device_config_page(struct tcp_pcb* tpcb, const char* message) {
    char page[8192];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

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
             "input[type='text'], input[type='number'] { width: 96%%; padding: 0.4em; font-size: 1em; box-sizing: border-box; }"
             "input[type='checkbox'], input[type='radio'] { width: auto; }"
             "input[type='submit'] { padding: 0.6em 1em; font-size: 1em; margin: 0.5em; width: 60%%; max-width: 200px; }"
             "fieldset { border: 1px solid #ccc; padding: 1em 1.2em; margin-top: 1em; text-align: left; }"
             "legend { font-weight: bold; }"
             ".message { margin: 1em auto; font-size: 1em; font-weight: bold; color: green; }"
             "</style></head><body>"
             "<h1>Device Configuration</h1>");

    if (message && *message) {
        snprintf(page + strlen(page), sizeof(page) - strlen(page),
                 "<div class='message'>%s</div>", message);
    }

    // Start of the form with general configuration grouped in a fieldset
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<form method=\"POST\" action=\"/device_config\">"
             "<fieldset><legend>Room Settings</legend>"

             // Room name input
             "<label>Room name:<br>"
             "<input type=\"text\" name=\"roomname\" value=\"%s\" maxlength=\"15\"></label>",
             device_config_flash.data.roomname);

    // Room type and number of seats
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<div style=\"margin-top:1em;\">"
             "<strong>Room type</strong><br>"
             "<label class=\"inline\"><input type=\"radio\" name=\"type\" value=\"0\" %s> Office</label>"
             "<label class=\"inline\"><input type=\"radio\" name=\"type\" value=\"1\" %s> Meeting</label>"
             "<label class=\"inline\"><input type=\"radio\" name=\"type\" value=\"2\" %s> Lecture hall</label>"
             "</div>"

             // Number of seats input
             "<label style=\"margin-top:1em; display:block;\">"
             "Number of seats:<br>"
             "<input type=\"number\" id=\"number_of_seats\" name=\"number_of_seats\" value=\"%d\" min=\"0\" max=\"5\">"
             "</label></fieldset>",
             (device_config_flash.data.type == 0 ? "checked" : ""),
             (device_config_flash.data.type == 1 ? "checked" : ""),
             (device_config_flash.data.type == 2 ? "checked" : ""),
             device_config_flash.data.number_of_seats);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<fieldset><legend>ePaper-Typ</legend>"
             "<label class=\"inline\"><input type=\"radio\" name=\"epapertype\" value=\"0\" %s onchange=\"updateSeatLimit()\"> None</label>"
             "<label class=\"inline\"><input type=\"radio\" name=\"epapertype\" value=\"1\" %s onchange=\"updateSeatLimit()\"> 7.5 Zoll</label>"
             "<label class=\"inline\"><input type=\"radio\" name=\"epapertype\" value=\"2\" %s onchange=\"updateSeatLimit()\"> 4.2 Zoll</label>"
             "</fieldset>",
             (device_config_flash.data.epapertype == 0 ? "checked" : ""),
             (device_config_flash.data.epapertype == 1 ? "checked" : ""),
             (device_config_flash.data.epapertype == 2 ? "checked" : ""));

    // Refresh-Intervalle
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<fieldset><legend>Refresh Intervals (minutes)</legend>");

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page (0): <input type=\"number\" name=\"refresh0\" value=\"%d\" min=\"1\" max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[0]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 1: <input type=\"number\" name=\"refresh1\" value=\"%d\" min=\"1\" max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[1]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 2: <input type=\"number\" name=\"refresh2\" value=\"%d\" min=\"1\" max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[2]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 3: <input type=\"number\" name=\"refresh3\" value=\"%d\" min=\"1\" max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[3]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 4: <input type=\"number\" name=\"refresh4\" value=\"%d\" min=\"1\" max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[4]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 5: <input type=\"number\" name=\"refresh5\" value=\"%d\" min=\"1\" max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[5]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 6: <input type=\"number\" name=\"refresh6\" value=\"%d\" min=\"1\" max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[6]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 7: <input type=\"number\" name=\"refresh7\" value=\"%d\" min=\"1\" max=\"1440\"></label>",
             device_config_flash.data.refresh_minutes_by_pushbutton[7]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page), "</fieldset>");

    // WiFi Settings section
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<fieldset><legend>WiFi Settings</legend>"

             // Number of WiFi attempts
             "<label>Number of WiFi Attempts:<br>"
             "<input type=\"number\" name=\"number_wifi_attempts\" value=\"%d\" min=\"1\" max=\"50\"></label>"

             "<label>WiFi Timeout (ms):<br>"
             "<input type=\"number\" name=\"wifi_timeout\" value=\"%d\" min=\"100\" max=\"10000\"></label>"

             "<label>Max Wait for Data (ms):<br>"
             "<input type=\"number\" name=\"max_wait_data_wifi\" value=\"%d\" min=\"10\" max=\"10000\"></label>"

             "<label>WiFi Reconnect Minutes:<br>"
             "<input type=\"number\" name=\"wifi_reconnect_minutes\" value=\"%d\" min=\"1\" max=\"30\"></label>"

             "</fieldset>",
             device_config_flash.data.number_wifi_attempts,
             device_config_flash.data.wifi_timeout,
             device_config_flash.data.max_wait_data_wifi,
             device_config_flash.data.wifi_reconnect_minutes);

    // Hardware settings section
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<fieldset><legend>Hardware</legend>"

             // Battery cutoff voltage
             "<label>Battery Cutoff Voltage (V):<br>"
             "<input type=\"number\" step=\"0.1\" name=\"switch_off_battery_voltage\" value=\"%.2f\" min=\"2.4\" max=\"3.9\"></label><br>"

             // Watchdog timeout
             "<label>Watchdog Timeout (ms):<br>"
             "<input type=\"number\" name=\"watchdog_time\" value=\"%d\" min=\"6000\" max=\"8000\"></label><br>"

             // Conversion factor
             "<label>Conversion Factor:<br>"
             "<input type=\"text\" name=\"conversion_factor\" value=\"%.6f\" step=\"any\"></label>"

             "</fieldset>",
             device_config_flash.data.switch_off_battery_voltage,
             device_config_flash.data.watchdog_time,
             device_config_flash.data.conversion_factor);

    // Checkboxes
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<div style=\"margin-top: 1em;\">"
             "<label class=\"inline\"><input type=\"checkbox\" name=\"show_query_date\" value=\"1\" %s> Show query timestamp</label><br>"
             "<label class=\"inline\"><input type=\"checkbox\" name=\"query_only_at_officehours\" value=\"1\" %s> Query only during office hours</label><br>"
             "</div>",
             (device_config_flash.data.show_query_date ? "checked" : ""),
             (device_config_flash.data.query_only_at_officehours ? "checked" : ""));

    // Submit, Footer
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<input type=\"submit\" value=\"Store\">"
             "</form>"
             "<a href=\"/\">back</a>"
             "<p>%s</p>"
             "</body></html>",
             timeout_info);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<script>"
             "function updateSeatLimit() {"
             "  var epaper = document.querySelector('input[name=\"epapertype\"]:checked').value;"
             "  var seats = document.getElementById('number_of_seats');"
             "  if (epaper == '2') {"
             "    seats.value = 1;"
             "    seats.max = 1;"
             "  } else if (epaper == '1') {"
             "    if (seats.value > 3) seats.value = 3;"
             "    seats.max = 3;"
             "  } else {"
             "    seats.max = 5;"
             "  }"
             "}"
             "window.onload = updateSeatLimit;"
             "</script>");

    debug_log("device settings page length: %d\n", strlen(page));
    send_response(tpcb, page);
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

    wifi_config_t new_cfg = {
        .crc32 = 0
    };
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

    seatsurfing_config_t new_cfg = { .crc32 = 0 };

    strncpy(new_cfg.data.host,        result.text[0], sizeof(new_cfg.data.host)        - 1);
    strncpy(new_cfg.data.username,    result.text[1], sizeof(new_cfg.data.username)    - 1);
    strncpy(new_cfg.data.password,    result.text[2], sizeof(new_cfg.data.password)    - 1);

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

    strncpy(new_cfg.data.space_id,    result.text[5], sizeof(new_cfg.data.space_id)    - 1);
    strncpy(new_cfg.data.location_id, result.text[6], sizeof(new_cfg.data.location_id) - 1);

    bool ok = save_seatsurfing_config(&new_cfg);
    if (ok) {
        debug_log_with_color(COLOR_YELLOW, "Seatsurfing-Konfiguration gespeichert.\n");
    } else {
        debug_log_with_color(COLOR_RED, "Fehler beim Speichern der Seatsurfing-Konfiguration.\n");
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

    historian_config_t new_cfg = { .crc32 = 0 };

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
    strncpy(new_cfg.data.path,         result.text[2], sizeof(new_cfg.data.path)         - 1);
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
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    parse_form_fields(body, len, &result);

    device_config_t new_cfg = { .crc32 = 0 }; // Important: struct contains .data + .crc32

    // Take over existing values
    memcpy(&new_cfg.data, &device_config_flash.data, sizeof(device_config_data_t));

    // Neue Werte eintragen
    strncpy(new_cfg.data.roomname, result.roomname, sizeof(new_cfg.data.roomname) - 1);
    new_cfg.data.type = (RoomType)result.type;
    new_cfg.data.epapertype = (EpaperType)result.epapertype;

    for (int i = 0; i < 8; i++) {
        new_cfg.data.refresh_minutes_by_pushbutton[i] = result.refresh_minutes_by_pushbutton[i];
    }

    new_cfg.data.number_of_seats = result.number_of_seats;
    new_cfg.data.show_query_date = result.show_query_date;
    new_cfg.data.query_only_at_officehours = result.query_only_at_officehours;
    new_cfg.data.wifi_reconnect_minutes = result.wifi_reconnect_minutes;
    new_cfg.data.watchdog_time = result.watchdog_time;
    new_cfg.data.number_wifi_attempts = result.number_wifi_attempts;
    new_cfg.data.wifi_timeout = result.wifi_timeout;
    new_cfg.data.max_wait_data_wifi = result.max_wait_data_wifi;
    new_cfg.data.conversion_factor = result.conversion_factor;

    bool ok = save_device_config(&new_cfg);

    send_device_config_page(tpcb, ok ? "✔ Device settings saved" : "⚠ Error saving settings");

    if (ok) {
        debug_log_with_color(COLOR_GREEN, "Device configuration saved\n");
    } else {
        debug_log_with_color(COLOR_RED, "Error saving device configuration\n");
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
    sscanf(line_param + 5, "%127[^&\r\n]", raw_line);  // robust
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
