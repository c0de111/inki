#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/altcp.h"
#include "lwip/ip_addr.h"
#include "hardware/adc.h"
#include "hardware/watchdog.h"
#include "config.h"
#include "version.h"
#include "wifi.h"
#include "DEV_Config.h"
#include "GUI_Paint.h"
#include "ImageResources.h"
#include "EPD_7in5_V2.h"
#include "EPD_4in2_V2.h"
#include "EPD_2in9_V2.h"
#include "ds3231.h"
#include "debug.h"
#include "http_client.h"
#include "flash.h"
#include "webserver.h"
#include "main.h"
#include "http_client.h"
#include "base64.h"
#include "historian_config.h"
#include "led.h"
#include "morse.h"
#include <math.h>
#include <unistd.h>
#include <stdint.h>
#include <math.h>

#if PICO_SDK_VERSION_MAJOR != 2 || PICO_SDK_VERSION_MINOR != 1 || PICO_SDK_VERSION_REVISION != 0
#warning "This firmware was developed and tested with pico-sdk 2.1.0. Other versions may cause issues."
#endif

#ifdef USE_CASE_HISTORIAN
// Historian data storage
static TimeSeries historian_data = {0};
extern size_t g_http_response_length; // From http_client.c

// Callback for Historian data (like esign)
void historian_data_received(const char* json_data, size_t length, void* arg) {
    if (!json_data || length == 0) {
        debug_log_with_color(COLOR_RED, "[HISTORIAN] Transfer failed or incomplete\n");
        return;
    }

    debug_log_with_color(COLOR_GREEN,
                         "[HISTORIAN] Received %d bytes of JSON data\n", (int)length);

    // Parse JSON into TimeSeries
    if (historian_parse_timeseries(json_data, &historian_data)) {
        debug_log("[HISTORIAN] Successfully parsed %d data points\n",
                  historian_data.count);
        debug_log("[HISTORIAN] Temperature range: %.2f - %.2f °C\n",
                  historian_data.min_value, historian_data.max_value);
        
        // Debug: Print first few data points like esign
        for (int i = 0; i < historian_data.count && i < 10; i++) {
            debug_log("[HISTORIAN] #%03d: timestamp=%" PRIu64 " ms UTC, "
                      "value=%.2f %s, state=%u\n",
                      i,
                      historian_data.points[i].timestamp,
                      historian_data.points[i].value,
                      historian_data.unit,
                      historian_data.points[i].state);
        }
    } else {
        debug_log_with_color(COLOR_RED, "[HISTORIAN] Failed to parse JSON\n");
    }
}
#endif

#ifdef USE_CASE_HOMEMATIC
#include "homematic_config.h"

typedef enum { HM_TYPE_NONE, HM_TYPE_DOUBLE, HM_TYPE_I4, HM_TYPE_BOOL, HM_TYPE_STRING } hm_type_t;
typedef struct {
    bool valid;
    bool fault;
    hm_type_t type;
    double dval;
    int ival;
    bool bval;
    char sval[32];
    char unit[8];
} hm_item_value_t;

static hm_item_value_t homematic_values[HOMEMATIC_MAX_ITEMS];

// Simple storage for service messages
#define HM_MAX_SERVICE_MSGS 5
static char homematic_service_msgs[HM_MAX_SERVICE_MSGS][80];
static char homematic_service_addr[HM_MAX_SERVICE_MSGS][24];
static int homematic_service_count = 0;

static void draw_toggle_control(int x, int y, const sFONT* f, bool is_on) {
    // Dimensions relative to font size
    int h = f->Height;              // baseline character height
    int th = h - 6;                 // toggle height
    if (th < 12) th = h - 4;        // keep minimum shape
    int top = y + (h - th) / 2;     // vertical centering relative to text baseline
    int bottom = top + th;
    int left = x;
    // Compute geometry and draw outer pill outline (keep outer lines with rounded ends)
    int r_track = th / 2;
    int cy = top + r_track;
    int lx = left + r_track;
    const char* txt = is_on ? "an" : "aus";
    int text_px = (int)strlen(txt) * f->Width;
    int r = r_track - 3; if (r < 4) r = 4;           // knob radius
    int straight_len = text_px + 2 * (r + 4);        // text + margins around knob
    int tw = 2 * r_track + straight_len;             // virtual width (for layout only)
    if (tw < th * 2) tw = th * 2;                    // ensure minimum 2:1 space
    int right = left + tw;
    int rx = right - r_track;

    // Outer pill outline (no inner hollow circles besides the filled knob)
    Paint_DrawLine(lx, top, rx, top, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(lx, bottom, rx, bottom, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawCircle(lx, cy, r_track, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    Paint_DrawCircle(rx, cy, r_track, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

    // Slider knob as filled circle
    int cx = is_on ? lx : rx; // left for "an", right for "aus"
    Paint_DrawCircle(cx, cy, r, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);

    // Text placed away from the knob
    int tx;
    if (is_on) {
        // knob left, text to the right of knob
        tx = lx + r + 4;
        if (tx + text_px > right - 2) tx = right - 2 - text_px;
    } else {
        // knob right, text to the left of knob
        tx = rx - r - 4 - text_px;
        if (tx < left + 2) tx = left + 2;
    }
    int ty = y; // align with surrounding baseline
    // Match global convention: WHITE as foreground draws dark text on light bg
    Paint_DrawString_EN(tx, ty, txt, (sFONT*)f, WHITE, BLACK);
}

static const char* derive_unit_for_key(const char* key) {
    if (!key) return "";
    // Temperature family
    if (strstr(key, "TEMP") || strcmp(key, "ACTUAL_TEMPERATURE") == 0 || strcmp(key, "TEMPERATURE") == 0) return "degC";
    // Relative humidity
    if (strstr(key, "HUMID")) return "%";
    // Voltage/Power common keys
    if (strstr(key, "VOLT")) return "V";
    if (strstr(key, "POWER")) return "W";
    if (strstr(key, "ILLUM") || strstr(key, "BRIGHT")) return "lux";
    return "";
}

// Summarize a CCU address like "000E5F29B4AE18:0" to "AE18:0"
static void summarize_addr(const char* full, char* out, size_t n) {
    if (!full || !out || n == 0) { return; }
    out[0] = 0;
    // Consider the device part before ':'
    const char* colon = strrchr(full, ':');
    const char* dev_end = colon ? colon : full + strlen(full);
    // Walk back 4 hex chars within device part
    const char* p = dev_end;
    int count = 0;
    while (p > full && count < 4) { p--; count++; }
    snprintf(out, n, "%.*s", count, p);
}

static void homematic_data_received(const char* body, size_t length, void* arg) {
    // Special reset signal from HTTP layer
    if ((uintptr_t)arg == 0xFFFF) {
        for (int i = 0; i < HOMEMATIC_MAX_ITEMS; i++) {
            homematic_values[i].valid = false;
            homematic_values[i].fault = false;
            homematic_values[i].type = HM_TYPE_NONE;
            homematic_values[i].unit[0] = 0;
        }
        homematic_service_count = 0;
        for (int i = 0; i < HM_MAX_SERVICE_MSGS; i++) {
            homematic_service_msgs[i][0] = 0;
            homematic_service_addr[i][0] = 0;
        }
        return;
    }

    // If arg is a valid index (sequential single-call mode), only update that index
    if ((uintptr_t)arg < HOMEMATIC_MAX_ITEMS) {
        int idx = (int)(uintptr_t)arg;
        // On failure/null body, mark fault
        if (!body || length == 0) {
            homematic_values[idx].fault = true;
            homematic_values[idx].valid = true;
            homematic_values[idx].type = HM_TYPE_NONE;
            return;
        }

        const char* p = body;
        const char* end = NULL;
        const char* q = NULL;
        // Find first </value> boundary to limit search
        end = strstr(p, "</value>");
        if (!end) end = body + length;

        if ((q = strstr(p, "<double>")) && q < end) {
            double v = atof(q + 8);
            homematic_values[idx].type = HM_TYPE_DOUBLE;
            homematic_values[idx].dval = v;
            homematic_values[idx].valid = true;
        } else if ((q = strstr(p, "<i4>")) && q < end) {
            int v = atoi(q + 4);
            homematic_values[idx].type = HM_TYPE_I4;
            homematic_values[idx].ival = v;
            homematic_values[idx].valid = true;
        } else if ((q = strstr(p, "<boolean>")) && q < end) {
            int v = atoi(q + 9);
            homematic_values[idx].type = HM_TYPE_BOOL;
            homematic_values[idx].bval = (v != 0);
            homematic_values[idx].valid = true;
        } else if ((q = strstr(p, "<string>")) && q < end) {
            const char* r = strstr(q, "</string>");
            size_t len = (r && r < end) ? (size_t)(r - (q + 8)) : 0;
            if (len > sizeof(homematic_values[idx].sval) - 1) len = sizeof(homematic_values[idx].sval) - 1;
            memcpy(homematic_values[idx].sval, q + 8, len);
            homematic_values[idx].sval[len] = 0;
            homematic_values[idx].type = HM_TYPE_STRING;
            homematic_values[idx].valid = true;
        } else if (strstr(p, "<fault>") && strstr(p, "</fault>")) {
            homematic_values[idx].fault = true;
            homematic_values[idx].valid = true;
        } else {
            // Unknown format, mark as fault
            homematic_values[idx].fault = true;
            homematic_values[idx].valid = true;
        }
        return;
    }

    // Unit updates: sentinel 0x8000 | idx, body carries unit string
    if (((uintptr_t)arg & 0x8000) && ((uintptr_t)arg & 0x7FFF) < HOMEMATIC_MAX_ITEMS) {
        int idx = (int)((uintptr_t)arg & 0x7FFF);
        size_t ul = (length < sizeof(homematic_values[idx].unit)-1) ? length : sizeof(homematic_values[idx].unit)-1;
        if (body && ul > 0) {
            memcpy(homematic_values[idx].unit, body, ul);
            homematic_values[idx].unit[ul] = 0;
            debug_log_with_color(COLOR_GREEN, "[HOMEMATIC] Unit stored idx=%d '%s'\n", idx, homematic_values[idx].unit);
        } else {
            homematic_values[idx].unit[0] = 0;
            debug_log_with_color(COLOR_YELLOW, "[HOMEMATIC] Unit empty for idx=%d\n", idx);
        }
        return;
    }

    // Service messages: sentinel 0x9000
    if ((uintptr_t)arg == 0x9000) {
        homematic_service_count = 0;
        if (body && length > 0) {
            const char* p = body;
            // Expected shape per CCU: array of [address, type, <boolean>1</boolean>]
            while (homematic_service_count < HM_MAX_SERVICE_MSGS && (p = strstr(p, "<array><data><value>"))) {
                const char* addr_start = p + strlen("<array><data><value>");
                // Robustly skip any nested tags (<array><data><value>, <string>, or whitespace) until plain text starts
                for (;;) {
                    while (*addr_start == ' ' || *addr_start == '\n' || *addr_start == '\r' || *addr_start == '\t') addr_start++;
                    if (*addr_start != '<') break;
                    const char* gt = strchr(addr_start, '>');
                    if (!gt) break; // malformed; fall through
                    addr_start = gt + 1;
                }
                // Address ends at next tag open or at </string>/</value>
                const char* addr_end = strchr(addr_start, '<');
                if (!addr_end) break;
                // Store address/channel as Name surrogate
                size_t alen = (size_t)(addr_end - addr_start);
                if (alen >= sizeof(homematic_service_addr[0])) alen = sizeof(homematic_service_addr[0]) - 1;
                memcpy(homematic_service_addr[homematic_service_count], addr_start, alen);
                homematic_service_addr[homematic_service_count][alen] = 0;

                // Type in next <value>
                const char* type_tag = strstr(addr_end, "<value>");
                if (!type_tag) break;
                const char* type_end = strstr(type_tag + 7, "</value>");
                if (!type_end) break;
                size_t tlen = (size_t)(type_end - (type_tag + 7));
                char typebuf[24];
                if (tlen >= sizeof(typebuf)) tlen = sizeof(typebuf) - 1;
                memcpy(typebuf, type_tag + 7, tlen);
                typebuf[tlen] = 0;

                // State in next <value>
                const char* state_tag = strstr(type_end, "<value>");
                if (!state_tag) break;
                bool active = false;
                const char* b = strstr(state_tag, "<boolean>");
                if (b) active = (atoi(b + 9) != 0);
                else {
                    const char* i4 = strstr(state_tag, "<i4>");
                    if (i4) active = (atoi(i4 + 4) != 0);
                }

                if (active) {
                    const char* msg = NULL;
                    // ASCII-safe labels (fonts lack umlauts)
                    if (!strcmp(typebuf, "UNREACH") || !strcmp(typebuf, "STICKY_UNREACH")) msg = "Geraetekommunikation gestoert";
                    else if (!strcmp(typebuf, "LOW_BAT") || !strcmp(typebuf, "LOW_BAT_ALARM")) msg = "Batterieladezustand gering";
                    else if (!strcmp(typebuf, "SABOTAGE")) msg = "Sabotage";
                    else if (!strcmp(typebuf, "ERROR")) msg = "Fehler";
                    else if (!strcmp(typebuf, "DUTYCYCLE")) msg = "Duty Cycle";
                    else if (!strcmp(typebuf, "RSSI_DEVICE")) msg = "Funkverbindung schwach";
                    else msg = typebuf; // fallback

                    snprintf(homematic_service_msgs[homematic_service_count], sizeof(homematic_service_msgs[0]), "%s", msg);
                    debug_log("[HOMEMATIC] Service[%d] addr=%s type=%s -> '%s'\n",
                              homematic_service_count,
                              homematic_service_addr[homematic_service_count],
                              typebuf,
                              homematic_service_msgs[homematic_service_count]);
                    homematic_service_count++;
                }

                p = state_tag + 7;
            }
        }
        debug_log("[HOMEMATIC] Parsed %d service messages\n", homematic_service_count);
        return;
    }

    // Batch mode (e.g., multicall): reset and fill sequentially
    for (int i = 0; i < HOMEMATIC_MAX_ITEMS; i++) {
        homematic_values[i].valid = false;
        homematic_values[i].fault = false;
        homematic_values[i].type = HM_TYPE_NONE;
    }
    if (!body || length == 0) return;

    const char* p = body;
    int idx = 0;
    while (idx < HOMEMATIC_MAX_ITEMS && (p = strstr(p, "<value>"))) {
        const char* end = strstr(p, "</value>");
        if (!end) break;
        const char* fault = strstr(p, "<fault>");
        if (fault && fault < end) {
            homematic_values[idx].fault = true;
            homematic_values[idx].valid = true;
            idx++;
            p = end + 8;
            continue;
        }
        const char* q;
        if ((q = strstr(p, "<double>")) && q < end) {
            double v = atof(q + 8);
            homematic_values[idx].type = HM_TYPE_DOUBLE;
            homematic_values[idx].dval = v;
            homematic_values[idx].valid = true;
        } else if ((q = strstr(p, "<i4>")) && q < end) {
            int v = atoi(q + 4);
            homematic_values[idx].type = HM_TYPE_I4;
            homematic_values[idx].ival = v;
            homematic_values[idx].valid = true;
        } else if ((q = strstr(p, "<boolean>")) && q < end) {
            int v = atoi(q + 9);
            homematic_values[idx].type = HM_TYPE_BOOL;
            homematic_values[idx].bval = (v != 0);
            homematic_values[idx].valid = true;
        } else if ((q = strstr(p, "<string>")) && q < end) {
            const char* r = strstr(q, "</string>");
            size_t len = (r && r < end) ? (size_t)(r - (q + 8)) : 0;
            if (len > sizeof(homematic_values[idx].sval) - 1) len = sizeof(homematic_values[idx].sval) - 1;
            memcpy(homematic_values[idx].sval, q + 8, len);
            homematic_values[idx].sval[len] = 0;
            homematic_values[idx].type = HM_TYPE_STRING;
            homematic_values[idx].valid = true;
        } else {
            homematic_values[idx].valid = false;
        }
        idx++;
        p = end + 8;
    }
}
#endif


ds3231_t ds3231; // RTC definition
// extern const wifi_config_t wifi_config_flash;
// extern const seatsurfing_config_t seatsurfing_config_flash;
// extern const device_config_t device_config_flash;

/*Represents the combined state of pushbuttons pbx pressed during startup, mapped to 2³:
* - Button 1 adds 1 if pressed.
* - Button 2 adds 2 if pressed.
* - Button 3 adds 4 if pressed.
* - Button 4 adds 8 if pressed (not implemented in hardware currently)
* This determines the page shown on ePaper, and the refreshtimes via .refresh_minutes_by_pushbutton
*/
int pushbutton = 0;
bool pb1 = false;
bool pb2 = false;
bool pb3 = false;

/**
 * @brief Draws a sub-image onto the ePaper buffer at the specified position.
 *
 * @param buffer Pointer to the target ePaper image buffer (e.g., BlackImage).
 * @param sub_image Pointer to the sub-image structure containing the image data and dimensions.
 * @param x X-coordinate for the top-left corner of the sub-image.
 * @param y Y-coordinate for the top-left corner of the sub-image.
 * @param room_config Pointer to the current room configuration (to determine ePaper dimensions).
 */
void DrawSubImage(UBYTE* buffer, const SubImage* sub_image, int x, int y) {
    // Get buffer dimensions dynamically
    int buffer_width, buffer_height;

    switch (device_config_flash.data.epapertype) {
        case EPAPER_WAVESHARE_7IN5_V2:
            buffer_width = EPD_7IN5_V2_WIDTH;
            buffer_height = EPD_7IN5_V2_HEIGHT;
            break;

        case EPAPER_WAVESHARE_4IN2_V2:
            buffer_width = EPD_4IN2_V2_WIDTH;
            buffer_height = EPD_4IN2_V2_HEIGHT;
            break;

        default:
            debug_log_with_color(COLOR_RED, "Unsupported ePaper type: %d\n", device_config_flash.data.epapertype);
            return;
    }

    // If Paint is in 4Gray mode, draw via Paint API to ensure proper 2bpp packing
    if (Paint.Scale == 4) {
        for (int j = 0; j < sub_image->height; j++) {
            for (int i = 0; i < sub_image->width; i++) {
                if (x + i >= buffer_width || y + j >= buffer_height) continue;
                int sub_index = (j * sub_image->width + i) / 8;
                int sub_bit = 7 - (i % 8);
                bool is_black = (sub_image->data[sub_index] & (1 << sub_bit)) != 0;
                UWORD col = is_black ? GRAY1 : GRAY4;
                Paint_SetPixel((UWORD)(x + i), (UWORD)(y + j), col);
            }
        }
    } else {
        // 1-bit buffer path: manipulate target buffer directly for speed
        for (int j = 0; j < sub_image->height; j++) {
            for (int i = 0; i < sub_image->width; i++) {
                if (x + i >= buffer_width || y + j >= buffer_height) continue;
                int buffer_index = ((y + j) * buffer_width + (x + i)) / 8;
                int buffer_bit = 7 - ((x + i) % 8);
                int sub_index = (j * sub_image->width + i) / 8;
                int sub_bit = 7 - (i % 8);
                if ((sub_image->data[sub_index] & (1 << sub_bit)) != 0) {
                    buffer[buffer_index] &= ~(1 << buffer_bit); // Black pixel
                } else {
                    buffer[buffer_index] |= (1 << buffer_bit);  // White pixel
                }
            }
        }
    }
}

/**
 * @brief Determines the battery level based on the voltage.
 *
 * This function checks the given voltage against a table of intervals
 * and returns the corresponding group value (percentage).
 *
 * @param voltage The measured voltage.
 * @param table Pointer to an array of VoltageInterval structures.
 * @param table_size The size of the voltage interval table.
 * @return The group value (percentage) corresponding to the voltage,
 *         or -1 if the voltage does not fall within any interval.
 */
int get_battery_level(float voltage, VoltageInterval *table, int table_size) {
    for (int i = 0; i < table_size; i++) {
        if (voltage >= table[i].voltage_min && voltage <= table[i].voltage_max) {
            return table[i].group_value;
        }
    }
    return -1; // Return -1 if no matching interval is found.
}

/**
 * @brief Displays the appropriate battery image based on the voltage.
 *
 * This function determines the battery level using the voltage and
 * displays the corresponding battery image on the screen.
 *
 * @param voltage The measured battery voltage.
 */
void display_battery_image(float voltage, UBYTE * image_buffer, int x, int y) {
    // Initialize the voltage interval table.
    VoltageInterval interval_table[] = {
        {10, 2.8, 3.4130},
        {20, 3.4130, 3.6830},
        {30, 3.6830, 3.8000},
        {40, 3.8000, 3.8910},
        {50, 3.8910, 3.9575},
        {60, 3.9575, 4.0240},
        {70, 4.0240, 4.0830},
        {80, 4.0830, 4.2290},
        {90, 4.2290, 4.2970},
        {100, 4.2970, 4.9}
    };

    int table_size = sizeof(interval_table) / sizeof(interval_table[0]);
    int battery_level = get_battery_level(voltage, interval_table, table_size);

    // Select and draw the appropriate battery image.
    if (battery_level == 10) {
        DrawSubImage(image_buffer, &battery_levels_64x97[BATTERY_LEVEL_1], x, y);
    } else if (battery_level == 20) {
        DrawSubImage(image_buffer, &battery_levels_64x97[BATTERY_LEVEL_2], x, y);
    } else if (battery_level == 30) {
        DrawSubImage(image_buffer, &battery_levels_64x97[BATTERY_LEVEL_3], x, y);
    } else if (battery_level == 40) {
        DrawSubImage(image_buffer, &battery_levels_64x97[BATTERY_LEVEL_4], x, y);
    } else if (battery_level == 50) {
        DrawSubImage(image_buffer, &battery_levels_64x97[BATTERY_LEVEL_5], x, y);
    } else if (battery_level == 60) {
        DrawSubImage(image_buffer, &battery_levels_64x97[BATTERY_LEVEL_6], x, y);
    } else if (battery_level == 70) {
        DrawSubImage(image_buffer, &battery_levels_64x97[BATTERY_LEVEL_7], x, y);
    } else if (battery_level == 80) {
        DrawSubImage(image_buffer, &battery_levels_64x97[BATTERY_LEVEL_8], x, y);
    } else if (battery_level == 90) {
        DrawSubImage(image_buffer, &battery_levels_64x97[BATTERY_LEVEL_9], x, y);
    } else if (battery_level == 100) {
        DrawSubImage(image_buffer, &battery_levels_64x97[BATTERY_LEVEL_10], x, y);
    } else {
        printf("Voltage %.2f is out of range!\n", voltage);
    }
}

/**
 * @brief Converts the day of the week from an integer (1-7) to its string representation.
 *
 * @param day An integer representing the day of the week (1 = Monday, 7 = Sunday).
 * @return A pointer to a constant string containing the name of the day.
 *         Returns "Invalid" if the input is out of range.
 */
const char* get_day_of_week(int day) {
    switch (day) {
        case 1: return "Monday";
        case 2: return "Tuesday";
        case 3: return "Wednesday";
        case 4: return "Thursday";
        case 5: return "Friday";
        case 6: return "Saturday";
        case 7: return "Sunday";
        default: return "Invalid";
    }
}

/**
 * @brief Converts the month from an integer (1-12) to its string representation.
 *
 * @param month An integer representing the month (1 = January, 12 = December).
 * @return A pointer to a constant string containing the name of the month.
 *         Returns "Invalid" if the input is out of range.
 */
const char* get_month_name(int month) {
    switch (month) {
        case 1: return "January";
        case 2: return "February";
        case 3: return "March";
        case 4: return "April";
        case 5: return "May";
        case 6: return "June";
        case 7: return "July";
        case 8: return "August";
        case 9: return "September";
        case 10: return "October";
        case 11: return "November";
        case 12: return "December";
        default: return "Invalid";
    }
}

/**
 * @brief Determines if DST (Daylight Saving Time) is active in Central Europe (MEZ/MESZ).
 *
 * This version takes a ds3231_data_t struct representing local standard time
 * (e.g., MEZ) and checks whether DST would be active for the given time.
 *
 * @param t Pointer to a ds3231_data_t containing local standard time.
 * @return true if DST is active (MESZ), false otherwise (MEZ).
 */
bool is_dst_europe(const ds3231_data_t* t) {
    int year = 2000 + t->year;
    int month = t->month;
    int day = t->date;
    int hour = t->hours;
    int minute = t->minutes;

    if (month < 3 || month > 10) return false;
    if (month > 3 && month < 10) return true;

    // Calculate last Sunday of March or October
    int last_sunday = 31 - ((5 * year / 4 + (month == 3 ? 4 : 1)) % 7);

    if (month == 3) {
        return (day > last_sunday) ||
        (day == last_sunday && (hour > 1 || (hour == 1 && minute >= 0)));
    } else { // October
        return (day < last_sunday) ||
        (day == last_sunday && (hour < 2 || (hour == 2 && minute == 0)));
    }
}

/**
 * @brief Formats the RTC time and date into a human-readable string.
 *
 * Produces a string in the format: "HH:MM, Day, DD. Month YYYY".
 *
 * Example: "21:23, Saturday, 13. January 2025"
 *
 * @param hours The hour in 24-hour format (0-23).
 * @param minutes The minute (0-59).
 * @param day The day of the week (1 = Monday, 7 = Sunday).
 * @param date The day of the month (1-31).
 * @param month The month (1 = January, 12 = December).
 * @param year The full year (e.g., 2025).
 * @param buffer A pointer to the buffer where the formatted string will be written.
 * @param buffer_size The size of the buffer.
 */

/**
 * @brief Formats the RTC time and date into a human-readable string with DST correction.
 *
 * Produces a string in the format: "HH:MM, Day, DD. Month YYYY".
 * Example: "21:23, Saturday, 13. January 2025"
 *
 * The RTC time is assumed to be in local standard time (e.g. MEZ).
 * If DST is active, one hour is added to the display time.
 *
 * @param t Pointer to ds3231_data_t containing RTC time (in standard time).
 * @param buffer A pointer to the buffer where the formatted string will be written.
 * @param buffer_size The size of the buffer.
 */
void format_rtc_time(const ds3231_data_t* t, char* buffer, size_t buffer_size) {
    int display_hour = t->hours;

    if (is_dst_europe(t)) {
        display_hour += 1;
        if (display_hour >= 24) display_hour -= 24;
    }

    snprintf(buffer, buffer_size, "%02i:%02i, %s, %02i. %s %04i",
             display_hour,
             t->minutes,
             get_day_of_week(t->day),
             t->date,
             get_month_name(t->month),
             2000 + t->year);
}

// Output: "13:45"
void format_short_time(const ds3231_data_t* t, char* buffer, size_t buffer_size) {
    int hour = t->hours;
    if (is_dst_europe(t)) {
        hour += 1;
        if (hour >= 24) hour -= 24;
    }
    snprintf(buffer, buffer_size, "%02i:%02i", hour, t->minutes);
}

void read_mac_address() {
    // Initialize the MAC address buffer
    memset(mac_address, 0, sizeof(mac_address));

    // Initialize the CYW43 driver with a country setting
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_GERMANY)) {
        debug_log_with_color(COLOR_RED, "CYW43 initialization failed.\n");
        return;
    }
    debug_log_with_color(COLOR_GREEN, "CYW43 initialized successfully.\n");

    // Enable station mode
    cyw43_arch_enable_sta_mode();

    // Retrieve the MAC address
    if (cyw43_wifi_get_mac(&cyw43_state, 0, mac_address) != 0) {
        debug_log_with_color(COLOR_RED, "Failed to retrieve MAC address.\n");
        cyw43_arch_deinit();
        return;
    }

    // Print the retrieved MAC address
    debug_log_with_color(COLOR_BOLD_GREEN,
                         "MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                         mac_address[0], mac_address[1], mac_address[2],
                         mac_address[3], mac_address[4], mac_address[5]);

    // Deinitialize the CYW43 driver
    cyw43_arch_deinit();
    debug_log_with_color(COLOR_GREEN, "CYW43 deinitialized successfully.\n");
}

// Old HTTP functions removed - now using http_client.c

//  ---------------------start functions for data from server --------------------------------

typedef struct {
    bool is_available;
    char user_email[64];  // empty if available
    char desk_name[32];   // "Desk 3", "Platz 1", etc.
} seat_info_t;

// Forward declaration
seat_info_t parse_seat_info(const char* json);

// Global variable to store SeatSurfing data (accessible to display functions)
static seat_info_t seatsurfing_data = {0};

// Callback for SeatSurfing data (unified callback architecture)
void seatsurfing_data_received(const char* response_data, size_t length, void* arg) {
    if (!response_data || length == 0) {
        debug_log_with_color(COLOR_RED, "[SEATSURFING] Transfer failed or incomplete\n");
        return;
    }

    debug_log_with_color(COLOR_GREEN,
                         "[SEATSURFING] Received %d bytes of response data\n", (int)length);

    // Parse JSON into seat_info_t using existing parse_seat_info function
    seatsurfing_data = parse_seat_info(response_data);
    
    debug_log("[SEATSURFING] Parsed seat info - Available: %s, Occupant: %s\n",
              seatsurfing_data.is_available ? "YES" : "NO",
              seatsurfing_data.user_email[0] ? seatsurfing_data.user_email : "None");
}

seat_info_t parse_seat_info(const char* json) {
    seat_info_t info = {
        .is_available = true,
        .user_email = {0},
        .desk_name = {0}
    };

    // Parse "available"
    const char* avail = strstr(json, "\"available\":");
    if (avail) {
        avail += strlen("\"available\":");
        info.is_available = (strncmp(avail, "true", 4) == 0);
    }

    // Parse "userEmail" if not available
    if (!info.is_available) {
        const char* email = strstr(json, "\"userEmail\":\"");
        if (email) {
            email += strlen("\"userEmail\":\"");
            const char* end = strchr(email, '"');
            if (end) {
                size_t len = end - email;
                if (len >= sizeof(info.user_email)) len = sizeof(info.user_email) - 1;
                strncpy(info.user_email, email, len);
                info.user_email[len] = 0;
            }
        }
    }

    // Parse "name"
    const char* name = strstr(json, "\"name\":\"");
    if (name) {
        name += strlen("\"name\":\"");
        const char* end = strchr(name, '"');
        if (end) {
            size_t len = end - name;
            if (len >= sizeof(info.desk_name)) len = sizeof(info.desk_name) - 1;
            strncpy(info.desk_name, name, len);
            info.desk_name[len] = 0;
        }
    }

    return info;
}


/**
 * @brief Configures and reads the state of pushbuttons
 *
 * This function sets up the GPIO pins for all pushbuttons as defined in the `RoomConfig` struct.
 * It also reads their states and updates the global `pushbutton` variable to reflect the combination
 * of buttons pressed. The function accommodates configurations with up to 3 pushbuttons currently (possible to increase in future versions), handling
 * variations in the number of buttons across different rooms or devices.
 *
 * Pushbutton states are read and their corresponding values
 * are accumulated into the `pushbutton` variable:
 * - Button 1 adds 1 if pressed.
 * - Button 2 adds 2 if pressed.
 * - Button 3 adds 4 if pressed.
 * - Button 4 adds 8 if pressed. (not implemented yet)
 *  -> pushbutton represents the pushbuttons pressed during startup, mapped to 2³
 *
 * **Debugging Note:** Ensure the `RoomConfig` struct correctly specifies the `num_pushbuttons`
 * and valid GPIO pin numbers for each pushbutton. Invalid or uninitialized values can cause
 * buttons to behave unexpectedly.
 *
 * @param config Pointer to the `RoomConfig` struct containing pushbutton configurations.
 */

void setup_and_read_pushbuttons() {
    // Reset pushbutton state
    pushbutton = 0;

    // --- Read GP0/GP1/GP2 strap pull-ups (10k to 3V3 if populated) ---
    {
        const uint strap_pins[4] = {0, 1, 2, 3};   // GP0..GP3
        uint8_t strap_bits = 0;

        for (int i = 0; i < 4; i++) {
            uint pin = strap_pins[i];
            gpio_init(pin);
            gpio_set_dir(pin, GPIO_IN);
            gpio_pull_down(pin);           // bias low; external 10k to 3V3 will override
            sleep_ms(1);                   // settle briefly
            int val = gpio_get(pin);       // 1 if pull-up populated, 0 if open
            strap_bits |= (val & 1) << i;  // bit0=GP0, bit1=GP1, bit2=GP2
            gpio_disable_pulls(pin);       // remove bias to avoid any static current
        }

        debug_log("Strap read GP3..GP0 = %d%d%d%d (mask=0x%02X)\n",
                  (strap_bits >> 3) & 1, (strap_bits >> 2) & 1,
                  (strap_bits >> 1) & 1, strap_bits & 1, strap_bits);
    }

    // Setup and read pushbutton 1
    if (device_config_flash.data.num_pushbuttons >= 1 && device_config_flash.data.pushbutton1_pin != 0xFF) {
        gpio_init(device_config_flash.data.pushbutton1_pin);
        gpio_set_dir(device_config_flash.data.pushbutton1_pin, GPIO_IN);
        gpio_pull_up(device_config_flash.data.pushbutton1_pin);
        sleep_ms(5); // De-bounce
        pb1 = gpio_get(device_config_flash.data.pushbutton1_pin); // Read pushbutton state
        if (!pb1) pushbutton += 1; // Active low
    }

    // Setup and read pushbutton 2
    if (device_config_flash.data.num_pushbuttons >= 2 && device_config_flash.data.pushbutton2_pin != 0xFF) {
        gpio_init(device_config_flash.data.pushbutton2_pin);
        gpio_set_dir(device_config_flash.data.pushbutton2_pin, GPIO_IN);
        gpio_pull_up(device_config_flash.data.pushbutton2_pin);
        sleep_ms(5); // De-bounce
        pb2 = gpio_get(device_config_flash.data.pushbutton2_pin); // Read pushbutton state
        if (!pb2) pushbutton += 2; // Active low
    }

    // Setup and read pushbutton 3
    if (device_config_flash.data.num_pushbuttons >= 3 && device_config_flash.data.pushbutton3_pin != 0xFF) {
        gpio_init(device_config_flash.data.pushbutton3_pin);
        gpio_set_dir(device_config_flash.data.pushbutton3_pin, GPIO_IN);
        gpio_pull_up(device_config_flash.data.pushbutton3_pin);
        sleep_ms(5); // De-bounce
        pb3 = gpio_get(device_config_flash.data.pushbutton3_pin); // Read pushbutton state
        if (!pb3) pushbutton += 4; // Active low
    }
    // for possible future use, maybe examples like below make sense
    // // Setup and read pushbutton 4
    // if (device_config_flash.data.num_pushbuttons == 4 && device_config_flash.data.pushbutton4_pin != 0xFF) {
    //     gpio_init(device_config_flash.data.pushbutton4_pin);
    //     gpio_set_dir(device_config_flash.data.pushbutton4_pin, GPIO_IN);
    //     gpio_pull_up(device_config_flash.data.pushbutton4_pin);
    //     sleep_ms(5); // De-bounce
    //     bool pb4 = gpio_get(device_config_flash.data.pushbutton4_pin); // Read pushbutton state
    //     if (!pb4) pushbutton += 8; // Active low
    // }
}


/**
 * Reads the battery voltage using the ADC.
 *
 * This function initializes the ADC, reads the voltage on GPIO26, and applies
 * the provided conversion factor to calculate the actual voltage.
 *
 * Parameters:
 * - conversion_factor: The factor used to convert the ADC reading to voltage.
 *
 * Returns:
 * - The measured battery voltage (float).
 */
float read_battery_voltage(float conversion_factor) {
    // debug_log("Reading battery voltage...");

    // Initialize ADC
    adc_init();
    adc_gpio_init(26);      // Initialize GPIO26 for ADC
    adc_select_input(0);    // Select ADC input 0 (GPIO26)

    // Read ADC value
    uint16_t adc_result = adc_read();
    float voltage = adc_result * conversion_factor;

    debug_log("Battery voltage: %.3f V\n", voltage);
    fflush(stdout);
    stdio_flush();
    return voltage;
}

/**
 * Reads the coin cell voltage using the ADC and a controlled MOSFET.
 *
 * This function enables the voltage divider via a MOSFET, reads the voltage on GPIO27,
 * and applies the provided conversion factor to calculate the actual voltage.
 *
 * Parameters:
 * - conversion_factor: The factor used to convert the ADC reading to voltage.
 *
 * Returns:
 * - The measured coin cell voltage (float).
 */
float read_coin_cell_voltage(float conversion_factor) {
    const uint gpio_mosfet = 14;  // GP14 controls the MOSFET

    // debug_log("Reading coin cell voltage...");

    // Initialize the MOSFET control pin
    gpio_init(gpio_mosfet);
    gpio_set_dir(gpio_mosfet, GPIO_OUT);

    // Turn ON the MOSFET to connect the voltage divider
    gpio_put(gpio_mosfet, 1);
    sleep_ms(5);  // Small delay to allow voltage to stabilize

    // Initialize ADC
    adc_init();
    adc_gpio_init(27);      // Initialize GPIO27 for ADC
    adc_select_input(1);    // Select ADC input 1 (GPIO27)

    // Read ADC value
    uint16_t adc_result = adc_read();
    float voltage = adc_result * conversion_factor;

    // Turn OFF the MOSFET after reading
    gpio_put(gpio_mosfet, 0);

    debug_log("Coin cell voltage: %.3f V\n", voltage);
    fflush(stdout);
    stdio_flush();
    return voltage;
}

/**
 * Reads the RP2040 on-chip temperature sensor and returns temperature in °C.
 *
 * Uses ADC input 4 (internal sensor). Takes multiple samples and averages
 * to reduce noise. Leaves the temperature sensor disabled after reading.
 */
float read_onchip_temperature_c(void) {
    const int samples = 8;  // simple averaging for stability
    uint32_t acc = 0;

    // Initialize and select internal temperature sensor (ADC input 4)
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);

    // Discard one initial sample (settling)
    (void)adc_read();

    for (int i = 0; i < samples; i++) {
        acc += adc_read();
    }

    adc_set_temp_sensor_enabled(false);

    float avg_raw = (float)acc / (float)samples;
    // Convert raw 12-bit reading to voltage assuming 3.3V reference
    float v_sense = avg_raw * 3.3f / 4096.0f;
    // Datasheet-calibrated typical values: 27°C at 0.706V, slope 1.721mV/°C
    float temp_c = 27.0f - (v_sense - 0.706f) / 0.001721f;

    debug_log("On-chip temperature: %.1f °C\n", temp_c);
    return temp_c;
}

/**
 * Reads the external DS3231 temperature in °C via I2C.
 * Returns NaN if the read fails.
 */
float read_ds3231_temperature_c(void) {
    extern ds3231_t ds3231;
    float t = 0.0f;
    if (ds3231_read_temperature(&ds3231, &t) != 0) {
        debug_log_with_color(COLOR_RED, "DS3231 temperature read failed\n");
        return NAN;
    }
    debug_log("DS3231 temperature: %.2f °C\n", t);
    return t;
}

// Linker-provided symbols (see linker script):
extern char __bss_end__;
extern char __StackLimit; // maximum heap ptr (stack region below)

void get_memory_info(memory_info_t* out) {
    if (!out) return;
    // Current heap end
    uintptr_t heap_end = (uintptr_t)sbrk(0);
    uintptr_t heap_base = (uintptr_t)&__bss_end__;
    uintptr_t heap_limit = (uintptr_t)&__StackLimit;

    size_t heap_used = 0;
    size_t heap_headroom = 0;
    if (heap_end >= heap_base) heap_used = (size_t)(heap_end - heap_base);
    if (heap_limit >= heap_end) heap_headroom = (size_t)(heap_limit - heap_end);

    // Approximate stack margin on core 0: distance from current SP to heap end
    register uintptr_t sp_reg __asm__("sp");
    size_t stack_margin = 0;
    if (sp_reg >= heap_end) stack_margin = (size_t)(sp_reg - heap_end);

    out->heap_used_bytes = heap_used;
    out->heap_headroom_bytes = heap_headroom;
    out->stack_margin_bytes = stack_margin;
    out->heap_base_addr = heap_base;
    out->heap_end_addr = heap_end;
    out->heap_limit_addr = heap_limit;
    out->sp_addr = sp_reg;
}

/**
 * Ensures the circuit remains powered by driving n-transistor (MMBT3904) -> MOSFET (TSM260P02) gate -> power up for running through programme, until switched off by the programme at the end
 *
 * Configures the specified GPIO pin as an output and sets it high
 * to maintain power after initial activation (e.g., by the gate pushbutton (GATE_PIN))
 * or clock trigger (DS3131)).
 *
 * Parameters:
 * - gate_pin: The GPIO pin controlling the power gate transistor.
 */

void hold_power(void) {
    gpio_init(GATE_PIN);
    gpio_set_dir(GATE_PIN, GPIO_OUT);
    gpio_put(GATE_PIN, 1); // Drive the gate pin high to keep power on
    debug_log("Gate Pin on -> Power switch on\n");
}

/**
 * Initializes the DS3231 RTC (Real-Time Clock) and sets up I2C communication.
 *
 * This function sets up the DS3231 clock structure and configures the necessary I2C pins.
 *
 * Returns:
 * - A fully initialized `ds3231_t` structure for RTC operations.
 */
    ds3231_t init_clock(void) {
    ds3231_t ds3231;

    // Initialize the DS3231 struct
    ds3231_init(&ds3231, i2c_default, DS3231_DEVICE_ADRESS, AT24C32_EEPROM_ADRESS_0);

    // Initialize I2C communication for the clock
    gpio_init(DS3231_SDA_PIN);
    gpio_init(DS3231_SCL_PIN);
    gpio_set_function(DS3231_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(DS3231_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(DS3231_SDA_PIN);
    gpio_pull_up(DS3231_SCL_PIN);
    i2c_init(ds3231.i2c, I2C_FREQ); // Set frequency to 400 kHz

    return ds3231;
}

int month_from_short_name(const char* name) {
    static const char* months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    for (int i = 0; i < 12; i++) {
        if (strncmp(name, months[i], 3) == 0) {
            return i + 1;
        }
    }
    return 0; // invalid
}

int weekday_from_name(const char* name) {
    const char* days[] = {
        "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
    };
    for (int i = 0; i < 7; i++) {
        if (strncmp(name, days[i], strlen(days[i])) == 0) {
            return i + 1; // DS3231: Monday = 1, ..., Sunday = 7
        }
    }
    return 0; // Invalid
}
void set_rtc_from_display_string(ds3231_t* ds3231, const char* line) {
    char weekday_str[10] = {0};
    char month_str[4] = {0};
    int day, month, year, hour, minute;

    debug_log("RTC set requested from line: ");
    debug_log(line);

    // Format: "Sunday, 21. Apr 2025, 13:45"
    int parsed = sscanf(line, "%9[^,], %d. %3s %d , %d:%d",
                        weekday_str, &day, month_str, &year, &hour, &minute);

    if (parsed != 6) {
        debug_log("RTC time parse failed.\n");
        debug_log("sscanf parsed items: ");
        char buf[2]; snprintf(buf, sizeof(buf), "%d", parsed);
        debug_log(buf);
        debug_log("\n");
        return;
    }

    int weekday = weekday_from_name(weekday_str);
    if (weekday == 0) {
        debug_log("Invalid weekday name.\n");
        return;
    }

    month = month_from_short_name(month_str);
    if (month == 0) {
        debug_log("Invalid month name in RTC string.\n");
        return;
    }

    ds3231_data_t temp = {
        .year = year - 2000,
        .month = month,
        .date = day,
        .hours = hour,
        .minutes = minute
    };

    if (is_dst_europe(&temp)) {
        hour -= 1;
        if (hour < 0) {
            hour += 24;
            day -= 1;

            if (day == 0) {
                debug_log("DST adjustment underflowed date — skipping RTC set.\n");
                return;
            }
        }
    }

    debug_log("Final time to set: ");
    char msg[64];
    snprintf(msg, sizeof(msg), "%02d:%02d %02d.%02d.%04d (weekday: %d)",
             hour, minute, day, month, year, weekday);
    debug_log(msg);

    ds3231_data_t new_time = {
        .seconds = 0,
        .minutes = minute,
        .hours = hour,
        .day = weekday,     // directly from server!
        .date = day,
        .month = month,
        .year = year - 2000,
        .century = 1,
        .am_pm = false
    };

    ds3231_configure_time(ds3231, &new_time);
    debug_log("RTC updated from server string using ds3231_configure_time().\n");
}

/**
 * @brief Sets the RTC alarm for the next wake-up time and configures the system for power-down.
 *
 * This function reads the current time from the RTC, calculates the next wake-up
 * time based on the room configuration, programs the alarm, and resets the power
 * gate pin to high impedance, allowing the RTC to control the system's power state.
 *
 * @param ds3231 Pointer to the initialized RTC structure.
 * @param room_config Pointer to the room configuration structure.
 *
 * @details
 * - The RTC holds regional **standard time** (e.g., MEZ), not UTC.
 * - If `query_only_at_officehours` is enabled in the room configuration, the wake-up time is
 *   adjusted to fall within office hours (6:00 AM to 7:00 PM), and operation is skipped on
 *   Saturdays and Sundays.
 * - The RTC alarm is configured in standard time by converting the calculated local alarm
 *   time (which includes DST) back to standard time if necessary.
 * - A safe modulo-based time calculation ensures refresh intervals > 60 min are handled correctly.
 * - The gate pin is reset to high impedance, enabling the RTC to control the power state.
 */
void set_alarmclock_and_powerdown(ds3231_t* ds3231) {
    ds3231_data_t current_time;
    ds3231_read_current_time(ds3231, &current_time);

    // Convert RTC standard time to local time (with DST)
    int local_hour = current_time.hours;
    int local_minute = current_time.minutes;
    int day = current_time.day; // 1 = Monday, 7 = Sunday

    bool dst_active = is_dst_europe(&current_time);
    if (dst_active) {
        local_hour += 1;
        if (local_hour >= 24) {
            local_hour -= 24;
            day = (day % 7) + 1; // wrap weekday 1–7
        }
    }

    // Calculate next wake-up time (safe for refresh_minutes ≥ 60)
    // int total_minutes = local_hour * 60 + local_minute + device_config_flash.data.refresh_minutes;
    int refresh = device_config_flash.data.refresh_minutes_by_pushbutton[pushbutton & 0x07]; // use only 3 bits
    int total_minutes = local_hour * 60 + local_minute + refresh;
    int alarm_hour = (total_minutes / 60) % 24;
    int alarm_minute = total_minutes % 60;

    // Adjust wake-up time based on office hours configuration
    if (device_config_flash.data.query_only_at_officehours) {
        // Skip operation on Saturdays (6) and Sundays (7)
        if (day == 6 || day == 7) {
            debug_log("Skipping operation: Weekend detected.\n");
            alarm_hour = 6;
            alarm_minute = 0;
            day = (day == 6) ? 7 : 1; // move to next day
        }

        // Clamp alarm to office hours (6:00 to 19:00)
        if (alarm_hour >= 19 || alarm_hour < 6) {
            alarm_hour = 6;
            alarm_minute = 0;
        }
    }

    // Convert alarm time from local time back to standard time (RTC base)
    if (dst_active) {
        alarm_hour -= 1;
        if (alarm_hour < 0) {
            alarm_hour += 24;
            // optional: decrement day if needed
        }
    }

    // Configure the RTC alarm
    ds3231_alarm_2_t alarm2 = {
        .minutes = alarm_minute,
        .hours = alarm_hour,
        .date = 0,
        .day = 0,
        .am_pm = false
    };

    ds3231_enable_alarm_interrupt(ds3231, true);
    ds3231_set_alarm_2(ds3231, &alarm2, ON_MATCHING_MINUTE_AND_HOUR);
    debug_log("Alarm2 set for %02d:%02d (RTC time)\n", alarm2.hours, alarm2.minutes);

    sleep_ms(5);

    // Reset the gate pin to high impedance to allow the RTC to control power
    gpio_set_dir(GATE_PIN, GPIO_IN);

    // Ensure the watchdog timer is updated before power-down
    watchdog_update();

    // Clear the alarm flag to allow the RTC to trigger the next wake-up
    ds3231_clear_alarm2(ds3231);
}

bool draw_flash_logo(UBYTE* buffer, int x, int y) {
    const logo_header_t* header = (const logo_header_t*)FLASH_PTR(LOGO_FLASH_OFFSET);

    if (memcmp(header->magic, "LOGO", 4) != 0) {
        debug_log_with_color(COLOR_YELLOW, "Kein gültiges Flash-Logo gefunden\n");
        return false;
    }

    debug_log("Flash-Logo gefunden: %dx%d px, %d bytes\n",
              header->width, header->height, header->datalen);

    const uint8_t* bitmap = FLASH_PTR(LOGO_FLASH_OFFSET + sizeof(logo_header_t));

    SubImage logo_image = {
        .data = bitmap,
        .width = header->width,
        .height = header->height
    };

    DrawSubImage(buffer, &logo_image, x, y);
    return true;
}

UBYTE* init_epaper() {

    if (device_config_flash.data.epapertype == EPAPER_NONE) {
        debug_log("No ePaper configured for this room.\n");
        return NULL;
    }

    watchdog_update();

    // Initialize the hardware module for the ePaper
    if (DEV_Module_Init() != 0) {
        debug_log("Error initializing ePaper hardware module.\n");
        return NULL;
    }

    // Disable the watchdog temporarily for long operations
    #ifdef HIGH_VERBOSE_DEBUG
    debug_log("Disabling watchdog for ePaper setup...\n");
    #endif
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);

    UDOUBLE Imagesize = 0;

    // Initialize and clear the ePaper based on the configured type
    switch (device_config_flash.data.epapertype) {
        case EPAPER_WAVESHARE_7IN5_V2:
            debug_log("Initializing Waveshare 7.5-inch V2 ePaper...\n");
#ifdef USE_CASE_WEATHERMAP
            EPD_7IN5_V2_Init_4Gray();
            EPD_7IN5_V2_Clear(); // Clear is needed to reset display content
            Imagesize = ((EPD_7IN5_V2_WIDTH % 8 == 0) ? (EPD_7IN5_V2_WIDTH / 4) : (EPD_7IN5_V2_WIDTH / 4 + 1)) * EPD_7IN5_V2_HEIGHT; // 4Gray: 2 bits per pixel
#else
            EPD_7IN5_V2_Init();
            EPD_7IN5_V2_Clear();
            Imagesize = ((EPD_7IN5_V2_WIDTH % 8 == 0) ? (EPD_7IN5_V2_WIDTH / 8) : (EPD_7IN5_V2_WIDTH / 8 + 1)) * EPD_7IN5_V2_HEIGHT;
#endif
            break;

        case EPAPER_WAVESHARE_4IN2_V2:
            debug_log("Initializing Waveshare 4.2-inch ePaper...\n");
#ifdef USE_CASE_WEATHERMAP
            // Waveshare official pattern: First clear in regular mode, then switch to 4Gray
            EPD_4IN2_V2_Init();
            EPD_4IN2_V2_Clear();
            sleep_ms(500); // Official timing from Waveshare examples
            EPD_4IN2_V2_Init_4Gray();
            Imagesize = ((EPD_4IN2_V2_WIDTH % 8 == 0) ? (EPD_4IN2_V2_WIDTH / 4) : (EPD_4IN2_V2_WIDTH / 4 + 1)) * EPD_4IN2_V2_HEIGHT; // 4Gray: 2 bits per pixel
#else
            EPD_4IN2_V2_Init();
            EPD_4IN2_V2_Clear();
            Imagesize = ((EPD_4IN2_V2_WIDTH % 8 == 0) ? (EPD_4IN2_V2_WIDTH / 8) : (EPD_4IN2_V2_WIDTH / 8 + 1)) * EPD_4IN2_V2_HEIGHT;
#endif
            break;

        case EPAPER_WAVESHARE_2IN9_V2:
            debug_log("Initializing Waveshare 2.9-inch V2 ePaper...\n");
            EPD_2IN9_V2_Init();
            EPD_2IN9_V2_Clear();
            Imagesize = ((EPD_2IN9_V2_WIDTH % 8 == 0) ? (EPD_2IN9_V2_WIDTH / 8) : (EPD_2IN9_V2_WIDTH / 8 + 1)) * EPD_2IN9_V2_HEIGHT;
            break;

        default:
            debug_log("Unsupported ePaper type: %d\n", device_config_flash.data.epapertype);
            hw_set_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS); // Re-enable watchdog
            return NULL;
    }

    // Re-enable the watchdog after setup
    #ifdef HIGH_VERBOSE_DEBUG
    debug_log("Re-enabling watchdog...\n");
    #endif

    watchdog_enable(device_config_flash.data.watchdog_time, 0);
    watchdog_update();

    // Create a new image cache
    UBYTE *BlackImage = (UBYTE *)malloc(Imagesize);
    if (BlackImage == NULL) {
        debug_log_with_color(COLOR_RED, "Failed to allocate memory for the image cache.\r\n");
        hw_set_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS); // Ensure watchdog is re-enabled
        return NULL;
    }

    #ifdef HIGH_VERBOSE_DEBUG
    
    #endif

    Paint_NewImage(BlackImage,
                   (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) ? EPD_7IN5_V2_WIDTH : EPD_4IN2_V2_WIDTH,
                   (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) ? EPD_7IN5_V2_HEIGHT : EPD_4IN2_V2_HEIGHT,
                   0, WHITE);

    #ifdef HIGH_VERBOSE_DEBUG
    
    #endif
    Paint_SelectImage(BlackImage);
#ifdef USE_CASE_WEATHERMAP
    Paint_SetScale(4); // Enable 4Gray mode for weathermap
    Paint_Clear(GRAY4); // Clear to white background
#else
    Paint_Clear(WHITE);
#endif

    watchdog_update();

    #ifdef HIGH_VERBOSE_DEBUG
    debug_log("ePaper setup completed.\n");
    #endif
    return BlackImage;

}

void format_name_from_email(const char* email, char* outbuf, size_t outbuf_len) {
    if (!email || !outbuf || outbuf_len < 2) {
        if (outbuf && outbuf_len > 0) outbuf[0] = '\0';
        return;
    }

    const char* at = strchr(email, '@');
    if (!at || at == email) {
        strncpy(outbuf, email, outbuf_len - 1);
        outbuf[outbuf_len - 1] = '\0';
        return;
    }

    size_t name_part_len = at - email;
    if (name_part_len >= outbuf_len) name_part_len = outbuf_len - 1;

    char name_part[64];
    strncpy(name_part, email, name_part_len);
    name_part[name_part_len] = '\0';

    char* dot = strchr(name_part, '.');
    if (dot) *dot = ' ';

    for (char* p = name_part; *p; ++p) {
        if (p == name_part || *(p - 1) == ' ') {
            *p = toupper(*p);
        } else {
            *p = tolower(*p);
        }
    }

    strncpy(outbuf, name_part, outbuf_len - 1);
    outbuf[outbuf_len - 1] = '\0';
}

/**
 * @brief Renders a 4Gray test pattern for validating grayscale display functionality
 * 
 * Displays a test pattern with:
 * - Title text demonstrating black text on white background
 * - Four circles showing each gray level: White, Light Gray, Dark Gray, Black
 * - Labels for each gray level
 * - Verification text
 * 
 * Useful for:
 * - Hardware validation of 4Gray ePaper displays
 * - Color mapping verification after driver changes
 * - Development reference for proper 4Gray usage
 */
void render_4gray_test_pattern(void) {
    // Title
    Paint_DrawString_EN(50, 30, "4-Level Grayscale Test", &font_ubuntu_mono_8pt_bold, GRAY4, GRAY1);
    
    // Draw 4 circles with different gray levels using Paint library + GRAY constants
    // Final mapping: GRAY1=0x00=Black, GRAY2=0x01=Dark Gray, GRAY3=0x02=Light Gray, GRAY4=0x03=White
    
    // Top row: Circle 1 & 2
    // Circle 1: White (GRAY4)
    Paint_DrawCircle(100, 100, 30, GRAY4, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(75, 140, "White", &font_ubuntu_mono_8pt, GRAY4, GRAY1);
    
    // Circle 2: Light Gray (GRAY3)
    Paint_DrawCircle(300, 100, 30, GRAY3, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(275, 140, "Light", &font_ubuntu_mono_8pt, GRAY4, GRAY1);
    
    // Bottom row: Circle 3 & 4
    // Circle 3: Dark Gray (GRAY2)
    Paint_DrawCircle(100, 200, 30, GRAY2, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(80, 240, "Dark", &font_ubuntu_mono_8pt, GRAY4, GRAY1);
    
    // Circle 4: Black (GRAY1)
    Paint_DrawCircle(300, 200, 30, GRAY1, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(275, 240, "Black", &font_ubuntu_mono_8pt, GRAY4, GRAY1);
    
    // Footer
  // Paint_DrawString_EN(50, 270, "4Gray validation complete", &font_ubuntu_mono_12pt, GRAY4, GRAY1);
}

#ifdef USE_CASE_HISTORIAN
/**
 * @brief Render temperature graph for historian data
 * @param image_buffer ePaper image buffer
 * @param x X position for graph
 * @param y Y position for graph  
 * @param width Graph width in pixels
 * @param height Graph height in pixels
 */
void render_temperature_graph(UBYTE* image_buffer, int x, int y, int width, int height) {
    if (historian_data.count == 0) {
        Paint_DrawString_EN(x + 10, y + height/2,
                            "Loading data...",
                            &font_ubuntu_mono_10pt, WHITE, BLACK);
        return;
    }

    // Draw frame
    Paint_DrawRectangle(x, y, x + width, y + height, BLACK,
                        DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

    // Draw horizontal grid lines
    for (int i = 1; i < 4; i++) {
        int grid_y = y + (height * i) / 4;
        Paint_DrawLine(x + 1, grid_y, x + width - 1, grid_y,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
    }

    // Draw vertical grid lines (10 Ticks)
    int tick_spacing = width / 10;

    // Time range in milliseconds
    uint64_t time_range_ms = historian_data.points[historian_data.count-1].timestamp -
    historian_data.points[0].timestamp;

    for (int i = 0; i <= 10; i++) {
        int tick_x = x + (i * tick_spacing);
        // Vertical line
        Paint_DrawLine(tick_x, y + 1, tick_x, y + height - 1,
                       BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);

        // Time labels for x-axis
        if (i % 2 == 0) {  // At 0, 2, 4, 6, 8, 10
            // Interpolate timestamp for this position
            uint64_t tick_time_ms = historian_data.points[0].timestamp +
            (time_range_ms * i) / 10;

            // Convert to seconds for time_t
            time_t tick_time_sec = tick_time_ms / 1000;

            // UTC to local time (MEZ/MESZ)
            struct tm* tick_tm = gmtime(&tick_time_sec);

            // Determine if DST applies for this date
            bool is_dst = false;
            if (tick_tm->tm_mon >= 2 && tick_tm->tm_mon <= 9) {  // March to October
                // Simplified: April-September = definitely DST
                if (tick_tm->tm_mon >= 3 && tick_tm->tm_mon <= 8) {
                    is_dst = true;
                }
            }

            // UTC -> MEZ/MESZ
            tick_time_sec += 3600;  // +1h for MEZ
            if (is_dst) {
                tick_time_sec += 3600;  // +1h additional for MESZ
            }

            // Convert again with adjusted time
            tick_tm = gmtime(&tick_time_sec);

            char time_label[8];
            snprintf(time_label, sizeof(time_label), "%02d:%02d",
                     tick_tm->tm_hour, tick_tm->tm_min);

            // Time below x-axis
            Paint_DrawString_EN(tick_x - 15, y + height + 5, time_label,
                                &font_ubuntu_mono_6pt, WHITE, BLACK);
        }
    }

    // Calculate scale
    float temp_range = historian_data.max_value - historian_data.min_value;
    if (temp_range < 0.1f) temp_range = 0.1f;

    // Draw temperature curve (with thicker line)
    int prev_x = -1, prev_y = -1;
    uint64_t start_time_ms = historian_data.points[0].timestamp;
    uint64_t end_time_ms = historian_data.points[historian_data.count-1].timestamp;
    uint64_t total_time_range_ms = end_time_ms - start_time_ms;

    for (int i = 0; i < historian_data.count; i++) {
        // X-position based on actual timestamp
        uint64_t time_offset_ms = historian_data.points[i].timestamp - start_time_ms;
        int point_x = x + (time_offset_ms * width) / total_time_range_ms;

        float normalized = (historian_data.points[i].value - historian_data.min_value) / temp_range;
        int point_y = y + height - (int)(normalized * height) - 1;

        // Clamp to graph bounds
        if (point_y < y) point_y = y;
        if (point_y > y + height) point_y = y + height;

        if (prev_x >= 0) {
            // Thicker line for better visibility
            Paint_DrawLine(prev_x, prev_y, point_x, point_y,
                           BLACK, DOT_PIXEL_3X3, LINE_STYLE_SOLID);
        }

        // Draw points for better visibility
        if (historian_data.count < 50) {  // Only if not too many points
            Paint_DrawCircle(point_x, point_y, 2, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        }

        prev_x = point_x;
        prev_y = point_y;
    }

    // Labels without degree symbol (not displayable)
    char label[32];

    // Min temperature (bottom left)
    snprintf(label, sizeof(label), "Min: %.1f C", historian_data.min_value);
    Paint_DrawString_EN(x + 5, y + height - 40, label,
                        &font_ubuntu_mono_9pt, WHITE, BLACK);

    // Max temperature (top left)
    snprintf(label, sizeof(label), "Max: %.1f C", historian_data.max_value);
    Paint_DrawString_EN(x + 5, y + 30, label,
                        &font_ubuntu_mono_9pt, WHITE, BLACK);

    // Current temperature (right side)
    snprintf(label, sizeof(label), "Now: %.1f C", historian_data.last_value);
    Paint_DrawString_EN(x + width - 240, y + height - 40, label,
                        &font_ubuntu_mono_12pt_bold, WHITE, BLACK);

    // Title with actual time range from data
    time_t start_sec = historian_data.points[0].timestamp / 1000;
    time_t end_sec = historian_data.points[historian_data.count-1].timestamp / 1000;

    // UTC -> Local time for display
    bool is_dst_now = true;  // August = MESZ
    start_sec += 3600 + (is_dst_now ? 3600 : 0);  // MEZ/MESZ
    end_sec += 3600 + (is_dst_now ? 3600 : 0);    // MEZ/MESZ

    struct tm* start_tm = gmtime(&start_sec);
    struct tm* end_tm = gmtime(&end_sec);

    char title[64];
    snprintf(title, sizeof(title), "Temperature (%02d:%02d - %02d:%02d %s)",
             start_tm->tm_hour, start_tm->tm_min,
             end_tm->tm_hour, end_tm->tm_min,
             is_dst_now ? "MESZ" : "MEZ");
    Paint_DrawString_EN(x, y - 20, title,
                        &font_ubuntu_mono_8pt, WHITE, BLACK);
}
#endif // USE_CASE_HISTORIAN

typedef void (*page_renderer_t)(ds3231_t* clock, UBYTE* image_buffer, float battery_voltage);

static void render_page_fallback(int pushbutton, ds3231_t* clock, UBYTE* image_buffer, float battery_voltage);
void render_page_wifi_setup(UBYTE* image);

#ifdef USE_CASE_HISTORIAN
static void render_page_0_historian(ds3231_t* clock, UBYTE* image_buffer, float battery_voltage)
{
    (void)clock;
    (void)battery_voltage;

    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {
        render_temperature_graph(image_buffer, 20, 20, 760, 400);

        char info[128];
        snprintf(info, sizeof(info), "%d data points", historian_data.count);
        Paint_DrawString_EN(50, 450, info, &font_ubuntu_mono_8pt, WHITE, BLACK);

        draw_flash_logo(image_buffer, 700, 10);

    } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {
        render_temperature_graph(image_buffer, 10, 10, 380, 250);

        char info[64];
        snprintf(info, sizeof(info), "%d points", historian_data.count);
        Paint_DrawString_EN(20, 280, info, &font_ubuntu_mono_8pt, WHITE, BLACK);
    } else {
        render_page_fallback(0, clock, image_buffer, battery_voltage);
    }
}

static void render_page_placeholder_historian(ds3231_t* clock, UBYTE* image_buffer, float battery_voltage)
{
    (void)clock;
    (void)battery_voltage;

    Paint_Clear(WHITE);

    const sFONT* font = &font_ubuntu_mono_14pt_bold;
    const sFONT* datetime_font = &font_ubuntu_mono_10pt;

    ds3231_data_t ds3231_data;
    ds3231_read_current_time(clock, &ds3231_data);

    char datetime_buf[64];
    format_rtc_time(&ds3231_data, datetime_buf, sizeof(datetime_buf));

    const char* title = "inki-historian";

    char page_line[32];
    snprintf(page_line, sizeof(page_line), "Page %d", pushbutton);

    const char* msg = "Not assigned";

    const int center_x_75 = 400;
    const int center_y_75 = 200;
    const int center_x_42 = 200;
    const int center_y_42 = 150;

    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {
        Paint_DrawString_EN(center_x_75 - 100, center_y_75 - 40, title, (sFONT*)font, WHITE, BLACK);
        Paint_DrawString_EN(center_x_75 - 60, center_y_75 + 10, page_line, (sFONT*)font, WHITE, BLACK);
        Paint_DrawString_EN(center_x_75 - 90, center_y_75 + 60, msg, (sFONT*)font, WHITE, BLACK);
        int logo_x = EPD_7IN5_V2_WIDTH - inki_octopus_100_95.width - 10;
        if (logo_x < 0) logo_x = 0;
        DrawSubImage(image_buffer, &inki_octopus_100_95, logo_x, 15);

        int datetime_y = EPD_7IN5_V2_HEIGHT - datetime_font->Height - 20;
        if (datetime_y < 0) datetime_y = 0;
        Paint_DrawString_EN(30, datetime_y, datetime_buf, (sFONT*)datetime_font, WHITE, BLACK);
    } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {
        int logo_x = EPD_4IN2_V2_WIDTH - inki_octopus_100_95.width - 10;
        if (logo_x < 0) logo_x = 0;
        DrawSubImage(image_buffer, &inki_octopus_100_95, logo_x, 15);
        Paint_DrawString_EN(center_x_42 - 80, center_y_42 - 40, title, (sFONT*)font, WHITE, BLACK);
        Paint_DrawString_EN(center_x_42 - 40, center_y_42 + 0, page_line, (sFONT*)font, WHITE, BLACK);
        Paint_DrawString_EN(center_x_42 - 60, center_y_42 + 40, msg, (sFONT*)font, WHITE, BLACK);
        int datetime_y = EPD_4IN2_V2_HEIGHT - datetime_font->Height - 15;
        if (datetime_y < 0) datetime_y = 0;
        Paint_DrawString_EN(20, datetime_y, datetime_buf, (sFONT*)datetime_font, WHITE, BLACK);
    } else {
        render_page_fallback(pushbutton, clock, image_buffer, battery_voltage);
    }
}

static void render_page_wifisetup_historian(ds3231_t* clock, UBYTE* image_buffer, float battery_voltage)
{
    (void)clock;
    (void)battery_voltage;
    render_page_wifi_setup(image_buffer);
}
#endif

// Render the default page with usecase-specific information and QR codes if enabled. This is the page without any user interaction
void render_page_0(ds3231_t* clock, UBYTE* image_buffer, float battery_voltage) {
    
#ifdef USE_CASE_SEATSURFING
    if (device_config_flash.data.type == ROOM_TYPE_OFFICE && device_config_flash.data.number_of_seats == 3 &&
        device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {

    // Display room name & logo
    Paint_DrawString_EN(40, 50, device_config_flash.data.roomname, &font_ubuntu_mono_28pt_bold,  WHITE, BLACK);

    // Use parsed data from callback (data processing already done)
    seat_info_t seat = seatsurfing_data;

    char linebuf[64];
    if (seat.is_available) {
        strncpy(linebuf, "frei", sizeof(linebuf));
    } else {
        format_name_from_email(seat.user_email, linebuf, sizeof(linebuf));
    }

    Paint_DrawString_EN(400, 320, linebuf, &font_ubuntu_mono_14pt_bold, WHITE, BLACK);

    // Draw a vertical separator line
    Paint_DrawLine(380, 170, 380, 300, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    }
    else if ((device_config_flash.data.type == ROOM_TYPE_CONFERENCE ) &&
        device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {

        Paint_DrawString_EN(70, 60, device_config_flash.data.roomname, &font_ubuntu_mono_28pt_bold,  WHITE, BLACK);
          }

    else if ((device_config_flash.data.type == ROOM_TYPE_OFFICE || device_config_flash.data.number_of_seats >= 1) &&
        device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {

        Paint_DrawString_EN(20, 40, device_config_flash.data.roomname, &font_ubuntu_mono_18pt_bold,  WHITE, BLACK);

    if (!draw_flash_logo(image_buffer, 290, 10)) {
        DrawSubImage(image_buffer, &inki_octopus_100_95, 290, 15);
    }

    // Use parsed data from callback (data processing already done)
    seat_info_t seat = seatsurfing_data;

    // Top line: desk name (e.g. "Desk 3")
    Paint_DrawString_EN(40, 220, seat.desk_name, &font_ubuntu_mono_14pt, WHITE, BLACK);

    // Second line: status ("frei" or formatted name)
    char linebuf[64];
    if (seat.is_available) {
        strcpy(linebuf, "frei");
    } else {
        format_name_from_email(seat.user_email, linebuf, sizeof(linebuf));
    }
    Paint_DrawString_EN(40, 150, linebuf, &font_ubuntu_mono_14pt_bold, WHITE, BLACK);
        }

#elif defined(USE_CASE_HOMEMATIC)
    // Simple list of Homematic values (page 0)
    Paint_DrawString_EN(20, 20, device_config_flash.data.roomname, &font_ubuntu_mono_14pt_bold, WHITE, BLACK);
    // Start a bit lower to add more distance from title
    // Shift query section down by 5 px (not the title)
    int y = 65;
    const sFONT* f_top = &font_ubuntu_mono_10pt;
    for (int i = 0; i < HOMEMATIC_MAX_ITEMS; i++) {
        if (i >= ((int)homematic_config_flash.data.count)) break;
        char label[48];
        const char* flab = homematic_config_flash.data.items[i].fallback_label;
        if (flab && *flab) snprintf(label, sizeof(label), "%s:", flab);
        else snprintf(label, sizeof(label), "%s:", homematic_config_flash.data.items[i].address);

        char val[64] = "—";
        if (homematic_values[i].valid && !homematic_values[i].fault) {
            switch (homematic_values[i].type) {
                case HM_TYPE_DOUBLE: snprintf(val, sizeof(val), "%.1f", homematic_values[i].dval); break;
                case HM_TYPE_I4:     snprintf(val, sizeof(val), "%d", homematic_values[i].ival); break;
                case HM_TYPE_BOOL:   val[0] = '\0'; break; // handled as toggle below
                case HM_TYPE_STRING: snprintf(val, sizeof(val), "%s", homematic_values[i].sval); break;
                default: break;
            }
        } else if (homematic_values[i].fault) {
            snprintf(val, sizeof(val), "fault");
        }

        // Render label, value and unit, drawing degree symbol manually if needed
        const sFONT* f = f_top;
        int x = 20;
        Paint_DrawString_EN(x, y, label, (sFONT*)f, WHITE, BLACK);
        x += (int)strlen(label) * f->Width + f->Width; // 1 space

        if (homematic_values[i].valid && homematic_values[i].type == HM_TYPE_BOOL) {
            // Draw high-contrast toggle for booleans
            draw_toggle_control(x, y, f, homematic_values[i].bval);
            x += (f->Height - 6) * 2 + f->Width; // advance by toggle width + space
        } else {
            Paint_DrawString_EN(x, y, val, (sFONT*)f, WHITE, BLACK);
            x += (int)strlen(val) * f->Width;
        }

        const char* unit = homematic_values[i].unit[0] ? homematic_values[i].unit
                              : derive_unit_for_key(homematic_config_flash.data.items[i].key);
        if (unit && unit[0] && !(homematic_values[i].valid && homematic_values[i].type == HM_TYPE_BOOL)) {
            if (strcmp(unit, "degC") == 0 || strcmp(unit, "°C") == 0) {
                // Draw small degree circle and then 'C'
                int r = (f->Width >= 12) ? 3 : 2;
                int cx = x + r + 1;                 // slight spacing after value
                int cy = y + r + 1;                 // lifted above baseline
                Paint_DrawCircle(cx, cy, r, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
                // Draw 'C' next to it with a small gap
                char cstr[2] = {'C', '\0'};
                Paint_DrawString_EN(cx + r + 1, y, cstr, (sFONT*)f, WHITE, BLACK);
            } else {
                char space_unit[16];
                snprintf(space_unit, sizeof(space_unit), " %s", unit);
                Paint_DrawString_EN(x, y, space_unit, (sFONT*)f, WHITE, BLACK);
            }
        }
        y += 30; // +2 px more spacing for readability
    }

    // Draw service messages at the bottom area
    if (homematic_service_count > 0) {
        // Use smaller font to fit more content per line
        const sFONT* small = &font_ubuntu_mono_6pt;
        int display_count = homematic_service_count;
        int by = Paint.Height - 10 - (display_count * (small->Height + 2));
        // Shift the service block up by 10 px
        by -= 10;
        if (by < y + 10) by = y + 10;
        Paint_DrawString_EN(20, by - (small->Height + 4), "Servicemeldungen:", (sFONT*)small, WHITE, BLACK);
        for (int i = 0; i < display_count; i++) {
            char short_addr[16];
            summarize_addr(homematic_service_addr[i], short_addr, sizeof(short_addr));

            // Compose line with ASCII dash (avoid UTF-8 em dash not in font)
            char line[160];
            snprintf(line, sizeof(line), "%s - %s", short_addr[0] ? short_addr : homematic_service_addr[i], homematic_service_msgs[i]);

            // Truncate to fit display width
            int max_cols = (Paint.Width - 40) / small->Width; // 20px margins left/right
            int len = (int)strlen(line);
            if (len > max_cols && max_cols > 3) {
                char tmp[160];
                int cut = max_cols - 3;
                if (cut > (int)sizeof(tmp) - 1) cut = (int)sizeof(tmp) - 1;
                memcpy(tmp, line, cut);
                memcpy(tmp + cut, "...", 3);
                tmp[cut + 3] = 0;
                strcpy(line, tmp);
            }

            debug_log("[HOMEMATIC] Render service: %s\n", line);
            Paint_DrawString_EN(20, by + i * (small->Height + 2), line, (sFONT*)small, WHITE, BLACK);
        }
    }

#elif defined(USE_CASE_WEATHERMAP)
    // WEATHERMAP: try to render stored 2-bit image from flash; fallback to test pattern
    extern bool weathermap_render_from_flash(void);
    if (!weathermap_render_from_flash()) {
        render_4gray_test_pattern();
        Paint_DrawString_EN(10, 10, "No map in flash", &font_ubuntu_mono_8pt_bold, WHITE, BLACK);
    }
    
#else
    // No use case defined - show error
    Paint_DrawString_EN(50, 100, "No use case configured", &font_ubuntu_mono_14pt, WHITE, BLACK);
    Paint_DrawString_EN(50, 130, "Check config.h", &font_ubuntu_mono_12pt, WHITE, BLACK);
#endif
}

/**
 * Render the "Do Not Disturb" page.
 * This page displays a message and the current time from the RTC.
 *
 * @param room  The room configuration containing ePaper and layout settings.
 * @param clock A pointer to the initialized RTC (ds3231) structure.
 */
void render_page_1(ds3231_t* clock, UBYTE* image_buffer, float battery_voltage) {
    char buffer[128]; // Buffer for formatted strings

    // Check the ePaper type and render accordingly
    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {
        DrawSubImage(image_buffer, &inki_octopus_100_95 , 270, 5);

        // Display room name
        Paint_DrawString_EN(70, 60, device_config_flash.data.roomname, &font_ubuntu_mono_28pt_bold,  WHITE, BLACK);


    } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {

        Paint_DrawString_EN(20, 40, device_config_flash.data.roomname, &font_ubuntu_mono_18pt_bold,  WHITE, BLACK);

        if (!draw_flash_logo(image_buffer, 290, 10)) {
            DrawSubImage(image_buffer, &inki_octopus_100_95, 290, 15);
        }

        sprintf(buffer, "Please,");
        Paint_DrawString_EN(50, 120, buffer, &font_ubuntu_mono_14pt_bold, WHITE, BLACK);
        sprintf(buffer, "Do Not Disturb!");
        Paint_DrawString_EN(50, 170, buffer, &font_ubuntu_mono_14pt_bold, WHITE, BLACK);

        // Read the current date and time from the RTC
        ds3231_data_t ds3231_data;
        ds3231_read_current_time(clock, &ds3231_data);

        // Format and display the current time as "Beginn"
        char time_string[8];
        format_short_time(&ds3231_data, time_string, sizeof(time_string));
        snprintf(buffer, sizeof(buffer), "Start: %s", time_string);

        Paint_DrawString_EN(70, 240, buffer, &font_ubuntu_mono_10pt, WHITE, BLACK);
        Paint_DrawString_EN(8, 292, "1", &Font8, WHITE, BLACK);

    } else {
        // Unsupported ePaper type, use default fallback
        debug_log("render_page_1 is not supported for the configured ePaper type.\n");

        if (!draw_flash_logo(image_buffer, 285, 10)) {
            DrawSubImage(image_buffer, &inki_octopus_100_95, 280, 15);
        }

    }
}

/**
* Render the "Universal Decision Maker" page.
* This page displays a message and a random "Yes" or "No" decision.
*
* @param room  The room configuration containing ePaper and layout settings.
* @param clock A pointer to the initialized RTC (ds3231) structure (not used here but kept for consistency).
*/
void render_page_2(ds3231_t* clock, UBYTE* image_buffer, float battery_voltage) {
    char buffer[128]; // Buffer for formatted strings
    ds3231_data_t ds3231_data;

    // Read the current time from the RTC
    // ds3231_read_current_time(clock, &ds3231_data);

    // Clear the ePaper display
    Paint_Clear(WHITE);

    // Check the ePaper type and render accordingly
    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {

        DrawSubImage(image_buffer, &inki_octopus_100_95, 30, 15);

        Paint_DrawString_EN(70, 60, device_config_flash.data.roomname, &font_ubuntu_mono_28pt_bold,  WHITE, BLACK); // Display room name

        // Rendering logic for the 7.5-inch ePaper
        sprintf(buffer, "Universal Decision Maker says:");
        Paint_DrawString_EN(25, 180, buffer, &font_ubuntu_mono_16pt, WHITE, BLACK);

        // Generate a random value and decide "Yes" or "No"
        uint8_t randValue = (uint8_t)get_rand_32();
        if (randValue > 127) {
            sprintf(buffer, "No!");
        } else {
            sprintf(buffer, "Yes!");
        }

        // Display the decision
        Paint_DrawString_EN(295, 280, buffer, &font_ubuntu_mono_36pt_bold, WHITE, BLACK);

        // format_rtc_time(&ds3231_data, buffer, sizeof(buffer));

        // Paint_DrawString_EN(40, 420, buffer, &font_ubuntu_mono_10pt, WHITE, BLACK);

    } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {
        DrawSubImage(image_buffer, &inki_octopus_100_95, 20, 15);

        sprintf(buffer, "Universal ");
        Paint_DrawString_EN(25, 40, buffer, &font_ubuntu_mono_11pt, WHITE, BLACK);
        sprintf(buffer, "Decision ");
        Paint_DrawString_EN(25, 70, buffer, &font_ubuntu_mono_11pt, WHITE, BLACK);
        sprintf(buffer, "Maker says:");
        Paint_DrawString_EN(25, 100, buffer, &font_ubuntu_mono_11pt, WHITE, BLACK);

        // Generate a random value and decide "Yes" or "No"
        uint8_t randValue = (uint8_t)get_rand_32();
        if (randValue > 127) {
            sprintf(buffer, "No!");
        } else {
            sprintf(buffer, "Yes!");
        }

        // Display the decision
        Paint_DrawString_EN(155, 180, buffer, &font_ubuntu_mono_22pt_bold, WHITE, BLACK);

        // format_rtc_time(&ds3231_data, buffer, sizeof(buffer));
        // Paint_DrawString_EN(20, 270, buffer, &font_ubuntu_mono_6pt, WHITE, BLACK);
        Paint_DrawString_EN(8, 292, "2", &Font8, WHITE, BLACK);

    } else {
        // Unsupported ePaper type, use default fallback
        debug_log("render_page_2 is not supported for the configured ePaper type.\n");

        if (!draw_flash_logo(image_buffer, 285, 10)) {
            DrawSubImage(image_buffer, &inki_octopus_100_95, 280, 15);
        }

    }
}

/**
 * Render the "Device Information and RTC" page.
 * This page displays device configuration, RTC time, and other key parameters.
 *
 * @param room  The room configuration containing ePaper and layout settings.
 * @param clock A pointer to the initialized RTC (ds3231) structure.
 */
void render_page_3(ds3231_t* clock, UBYTE* image_buffer, float battery_voltage) {
    char buffer[256]; // Buffer for formatted strings

    ds3231_data_t ds3231_data;

    // Read the current time from the RTC
    ds3231_read_current_time(clock, &ds3231_data);

    // Read the current battery voltage
    // float battery_voltage = read_battery_voltage(device_config_flash.data.conversion_factor);
    float coin_voltage = read_coin_cell_voltage(device_config_flash.data.conversion_factor);


    // Check the ePaper type and render accordingly
    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {
        // Display device information and RTC time
        DrawSubImage(image_buffer, &inki_octopus_100_95, 270, 5);
        Paint_DrawString_EN(70, 60, device_config_flash.data.roomname, &font_ubuntu_mono_28pt_bold,  WHITE, BLACK); // Display room name


    } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {
        // Display device information and RTC time
        if (!draw_flash_logo(image_buffer, 290, 10)) {
            DrawSubImage(image_buffer, &inki_octopus_100_95, 290, 15);
        }

        Paint_DrawString_EN(10, 20, device_config_flash.data.roomname, &font_ubuntu_mono_14pt_bold,  WHITE, BLACK);

        sprintf(buffer, "ssid: %s", wifi_config_flash.ssid);
        Paint_DrawString_EN(10, 70, buffer, &font_ubuntu_mono_6pt, WHITE, BLACK);

        sprintf(buffer, "wifi_reconnect_minutes: %i", device_config_flash.data.wifi_reconnect_minutes);
        Paint_DrawString_EN(10, 90, buffer, &font_ubuntu_mono_6pt, WHITE, BLACK);

        sprintf(buffer, "wifi_timeout: %i", device_config_flash.data.wifi_timeout);
        Paint_DrawString_EN(10, 110, buffer, &font_ubuntu_mono_6pt, WHITE, BLACK);

        sprintf(buffer, "refresh_minutes: [%d,%d,%d,%d,%d,%d,%d,%d]", device_config_flash.data.refresh_minutes_by_pushbutton[0], device_config_flash.data.refresh_minutes_by_pushbutton[1], device_config_flash.data.refresh_minutes_by_pushbutton[2], device_config_flash.data.refresh_minutes_by_pushbutton[3], device_config_flash.data.refresh_minutes_by_pushbutton[4], device_config_flash.data.refresh_minutes_by_pushbutton[5], device_config_flash.data.refresh_minutes_by_pushbutton[6], device_config_flash.data.refresh_minutes_by_pushbutton[7]);
        Paint_DrawString_EN(10, 130, buffer, &font_ubuntu_mono_6pt, WHITE, BLACK);

        // Get current time from RTC
        ds3231_data_t ds3231_data;
        ds3231_read_current_time(clock, &ds3231_data);

        // Format raw RTC time without DST
        char buffer2[256];
        snprintf(buffer, sizeof(buffer), "%02i:%02i, %s, %02i. %s %04i",
                 ds3231_data.hours,
                 ds3231_data.minutes,
                 get_day_of_week(ds3231_data.day),
                 ds3231_data.date,
                 get_month_name(ds3231_data.month),
                 2000 + ds3231_data.year);
        sprintf(buffer2, "RTC (raw): ");       // Copy the prefix
        strcat(buffer2, buffer);       // Append the original content

        Paint_DrawString_EN(10, 150, buffer2, &font_ubuntu_mono_6pt, WHITE, BLACK);

        format_rtc_time(&ds3231_data, buffer, sizeof(buffer));
        sprintf(buffer2, "RTC (DST): ");       // Copy the prefix
        strcat(buffer2, buffer);       // Append the original content
        Paint_DrawString_EN(10, 170, buffer2, &font_ubuntu_mono_6pt, WHITE, BLACK);

        read_mac_address();
        sprintf(buffer, "MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
                mac_address[0] & 0xFF, mac_address[1] & 0xFF, mac_address[2] & 0xFF,
                mac_address[3] & 0xFF, mac_address[4] & 0xFF, mac_address[5] & 0xFF);
        // Draw the MAC address on the ePaper
        Paint_DrawString_EN(10, 190, buffer, &font_ubuntu_mono_6pt, WHITE, BLACK);

        sprintf(buffer, "Vcc: %.3fV", battery_voltage);
        Paint_DrawString_EN(10, 210, buffer, &font_ubuntu_mono_6pt, WHITE, BLACK);

        sprintf(buffer, "Vbat: %.3fV", coin_voltage);
        Paint_DrawString_EN(10, 230, buffer, &font_ubuntu_mono_6pt, WHITE, BLACK);

        sprintf(buffer, "adc conv.: %.8f", device_config_flash.data.conversion_factor);
        Paint_DrawString_EN(10, 250, buffer, &font_ubuntu_mono_6pt, WHITE, BLACK);

        display_battery_image(battery_voltage, image_buffer, 330, 190);
        Paint_DrawString_EN(8, 292, "3", &Font8, WHITE, BLACK);

    } else {
        // Unsupported ePaper type, use default fallback
        debug_log("render_page_3 is not supported for the configured ePaper type.\n");
        DrawSubImage(image_buffer, &inki_octopus_100_95, 270, 5);
    }
}

// Howto page
void render_page_4(ds3231_t* clock, UBYTE* image_buffer, float battery_voltage){

    // Check the ePaper type and render accordingly
    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {
        // Display device information and RTC time
        DrawSubImage(image_buffer, &inki_octopus_100_95, 270, 5);
        Paint_DrawString_EN(70, 60, device_config_flash.data.roomname, &font_ubuntu_mono_28pt_bold,  WHITE, BLACK); // Display room name

      } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {

          int tempx = 5;
          // Title
          Paint_DrawString_EN(10, 10, "How to select pages", &font_ubuntu_mono_12pt_bold, WHITE, BLACK);

          Paint_DrawString_EN(10, 45, "Hold buttons 1-3 to select a page (1-7)", &font_ubuntu_mono_6pt, WHITE, BLACK);
          Paint_DrawString_EN(10, 63, "and press 'Start' at the same time", &font_ubuntu_mono_6pt, WHITE, BLACK);

          Paint_DrawString_EN(15, 85 + tempx, "page #0: 'default: room occupation'", &font_ubuntu_mono_6pt, WHITE, BLACK);
          Paint_DrawString_EN(15, 101 + tempx, "page #1: 'do not disturb'", &font_ubuntu_mono_6pt, WHITE, BLACK);
          Paint_DrawString_EN(15, 117 + tempx, "page #2: 'universal decision maker'", &font_ubuntu_mono_6pt, WHITE, BLACK);
          Paint_DrawString_EN(15, 133 + tempx, "page #3: 'display settings'", &font_ubuntu_mono_6pt, WHITE, BLACK);
          Paint_DrawString_EN(15, 149 + tempx, "page #4: 'How to show pages'", &font_ubuntu_mono_6pt, WHITE, BLACK);
          Paint_DrawString_EN(15, 165 + tempx, "page #5: 'set clock with server time'", &font_ubuntu_mono_6pt, WHITE, BLACK);
          Paint_DrawString_EN(15, 181 + tempx, "page #6: 'not used'", &font_ubuntu_mono_6pt, WHITE, BLACK);
          Paint_DrawString_EN(15, 197 + tempx, "page #7: 'not used'", &font_ubuntu_mono_6pt, WHITE, BLACK);

          // Button positions (X coordinates)
          const int positions[] = { 120, 180, 235, 290 };
          const char* labels[] = { "", "1", "2", "4" };

          // Draw arrows pointing down to pushbuttons
          int pb_y = 300;
          int arrow_bottom = pb_y - 1;
          int arrow_heights[] = { 17, 40, 40, 40 };  // PB0 shorter

          for (int i = 0; i < 4; i++) {
              int x = positions[i];
              int y_start = arrow_bottom - arrow_heights[i];

              // Arrow shaft
              Paint_DrawLine(x, y_start, x, arrow_bottom - 3, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);

              // Arrowhead
              Paint_DrawLine(x, arrow_bottom - 3, x - 3, arrow_bottom - 6, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
              Paint_DrawLine(x, arrow_bottom - 3, x + 3, arrow_bottom - 6, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
          }
          Paint_DrawString_EN(80, 260, "Start", &font_ubuntu_mono_8pt_bold, WHITE, BLACK);
          Paint_DrawString_EN(4, 240, " Selected page # = 1  +  2  +  4 ", &font_ubuntu_mono_6pt, WHITE, BLACK);

          // Page number
          Paint_DrawString_EN(8, 280, "4", &font_ubuntu_mono_8pt, WHITE, BLACK);

          Paint_DrawString_EN(320, 220, "more at", &font_ubuntu_mono_6pt, WHITE, BLACK);
          DrawSubImage(image_buffer, &qr_github_link, 330, 240);

    } else {
        // Unsupported ePaper type, use default fallback
        debug_log("render_page_3 is not supported for the configured ePaper type.\n");
        DrawSubImage(image_buffer, &inki_octopus_100_95, 270, 5);
    }
}

void render_page_5(ds3231_t* clock, UBYTE* image_buffer, float battery_voltage){
    const char* page_label = "Page 5: Setting RTC via WIFI to server time";
    int rtc_data_line = -1;

    // Determine which line to use based on display type
    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {
        rtc_data_line = 6;
    } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {
        if (!draw_flash_logo(image_buffer, 290, 10)) {
            DrawSubImage(image_buffer, &inki_octopus_100_95, 290, 15);
        }
        rtc_data_line = 4;
    }

    // Only proceed if a known display type is used
    if (rtc_data_line >= 0) {
        // Paint_DrawString_EN(20, 40, device_config_flash.data.roomname, &font_ubuntu_mono_18pt_bold, WHITE, BLACK);

        // Paint_DrawString_EN(5, 200, page_label, &font_ubuntu_mono_6pt, WHITE, BLACK);

        // const char *line = retrieveline(server_response_buf, rtc_data_line);
        // Paint_DrawString_EN(20, 270, line, &font_ubuntu_mono_6pt, WHITE, BLACK);
        //
        // set_rtc_from_display_string(clock, line);
        //
        // // Read current RTC time and format
        // ds3231_data_t ds3231_data;
        // ds3231_read_current_time(clock, &ds3231_data);
        //
        // char buffer[256];
        // char buffer2[256];
        //
        // Paint_DrawString_EN(5, 20, "Set RTC via server", &font_ubuntu_mono_12pt_bold, WHITE, BLACK);
        //
        // Paint_DrawString_EN(5, 60, "Current time fetched from the server ", &font_ubuntu_mono_6pt, WHITE, BLACK);
        // Paint_DrawString_EN(5, 80, "is written to the DS3231 real time clock", &font_ubuntu_mono_6pt,WHITE, BLACK);
        //
        // Paint_DrawString_EN(15, 140, "RTC (raw) set to: ", &font_ubuntu_mono_6pt, WHITE, BLACK);
        // snprintf(buffer, sizeof(buffer), "%02i:%02i, %s, %02i. %s %04i",
        //          ds3231_data.hours,
        //          ds3231_data.minutes,
        //          get_day_of_week(ds3231_data.day),
        //          ds3231_data.date,
        //          get_month_name(ds3231_data.month),
        //          2000 + ds3231_data.year);
        // snprintf(buffer2, sizeof(buffer2), "%s", buffer);
        // Paint_DrawString_EN(15, 160, buffer2, &font_ubuntu_mono_6pt, WHITE, BLACK);
        //
        // Paint_DrawString_EN(15, 200, "RTC (DST) set to: ", &font_ubuntu_mono_6pt, WHITE, BLACK);
        // format_rtc_time(&ds3231_data, buffer, sizeof(buffer));
        // snprintf(buffer2, sizeof(buffer2), "%s", buffer);
        // Paint_DrawString_EN(15, 220, buffer2, &font_ubuntu_mono_6pt, WHITE, BLACK);
        //
        // Paint_DrawString_EN(8, 292, "5", &Font8, WHITE, BLACK);
    }
}

void render_page_6(ds3231_t* clock, UBYTE* image_buffer, float battery_voltage){

    // Determine which epaper type is used
    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {
    } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {
        if (!draw_flash_logo(image_buffer, 290, 10)) {
            DrawSubImage(image_buffer, &inki_octopus_100_95, 290, 15);
        }
        Paint_DrawString_EN(330, 230, "more at", &font_ubuntu_mono_6pt, WHITE, BLACK);
        DrawSubImage(image_buffer, &qr_github_link, 340, 250);
        Paint_DrawString_EN(8, 292, "6", &Font8, WHITE, BLACK);
    }
}

void render_page_7(ds3231_t* clock, UBYTE* image_buffer, float battery_voltage){

    // Determine which epaper type is used
    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {
    } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {
        if (!draw_flash_logo(image_buffer, 290, 10)) {
            DrawSubImage(image_buffer, &inki_octopus_100_95, 290, 15);
        }

        Paint_DrawString_EN(130, 30, "Server Mode", &font_ubuntu_mono_11pt, WHITE, BLACK);

        Paint_DrawString_EN(330, 230, "more at", &font_ubuntu_mono_6pt, WHITE, BLACK);
        DrawSubImage(image_buffer, &qr_github_link, 340, 250);
        Paint_DrawString_EN(8, 292, "6", &Font8, WHITE, BLACK);
    }
}

static void render_page_fallback(int pushbutton, ds3231_t* clock, UBYTE* image_buffer, float battery_voltage) {
    (void)clock;
    (void)battery_voltage;
    debug_log("Invalid pushbutton state: %d\n", pushbutton);
    DrawSubImage(image_buffer, &inki_octopus_100_95, 270, 5);
}

// Render the appropriate page based on the RoomConfig and user-selected pushbutton state
void render_page(int pushbutton, ds3231_t* clock, UBYTE* image_buffer, float battery_voltage) {
    const page_renderer_t* table = NULL;
    size_t table_len = 0;

#if defined(USE_CASE_SEATSURFING)
    static const page_renderer_t seatsurfing_pages[8] = {
        render_page_0,
        render_page_1,
        render_page_2,
        render_page_3,
        render_page_4,
        render_page_5,
        render_page_6,
        render_page_7,
    };
    table = seatsurfing_pages;
    table_len = sizeof(seatsurfing_pages) / sizeof(seatsurfing_pages[0]);
#elif defined(USE_CASE_HISTORIAN)
    static const page_renderer_t historian_pages[8] = {
        render_page_0_historian,
        render_page_2,
        render_page_placeholder_historian,
        render_page_placeholder_historian,
        render_page_wifisetup_historian,
        render_page_placeholder_historian,
        render_page_placeholder_historian,
        render_page_placeholder_historian,
    };
    table = historian_pages;
    table_len = sizeof(historian_pages) / sizeof(historian_pages[0]);
#elif defined(USE_CASE_HOMEMATIC)
    static const page_renderer_t homematic_pages[8] = {
        render_page_0,
        render_page_1,
        render_page_2,
        render_page_3,
        render_page_4,
        render_page_5,
        render_page_6,
        render_page_7,
    };
    table = homematic_pages;
    table_len = sizeof(homematic_pages) / sizeof(homematic_pages[0]);
#elif defined(USE_CASE_WEATHERMAP)
    static const page_renderer_t weathermap_pages[8] = {
        render_page_0,
        render_page_1,
        render_page_2,
        render_page_3,
        render_page_4,
        render_page_5,
        render_page_6,
        render_page_7,
    };
    table = weathermap_pages;
    table_len = sizeof(weathermap_pages) / sizeof(weathermap_pages[0]);
#else
    static const page_renderer_t default_pages[8] = {
        render_page_0,
        render_page_1,
        render_page_2,
        render_page_3,
        render_page_4,
        render_page_5,
        render_page_6,
        render_page_7,
    };
    table = default_pages;
    table_len = sizeof(default_pages) / sizeof(default_pages[0]);
#endif

    page_renderer_t handler = NULL;
    if (table && table_len > 0 && pushbutton >= 0) {
        unsigned idx = (unsigned)pushbutton;
        if (idx < table_len) {
            handler = table[idx];
        }
    }

    if (handler) {
        handler(clock, image_buffer, battery_voltage);
    } else {
        render_page_fallback(pushbutton, clock, image_buffer, battery_voltage);
    }
}
/**
 * @brief Displays firmware version and battery status on the ePaper display.
 *
 * This function dynamically constructs and renders firmware information along with
 * the current battery voltage on the ePaper display. The displayed information includes:
 * - Program name
 * - Firmware version
 * - Build date
 * - Current battery voltage
 *
 * The function adapts the position and rendering style based on the ePaper type defined
 * in the `RoomConfig`. If the ePaper type is unsupported, a debug log message is generated.
 *
 * @note
 * - `program_name`, `version`, and `build_date` are extern variables defined in `version.h`
 *   and initialized in `version.c`.
 * - The rendering coordinates and font are adjusted based on the specific ePaper type.
 *
 * @param battery_voltage The current battery voltage, typically measured or calculated
 *                        during initialization.
 * @param room Pointer to the `RoomConfig` structure, which defines the room-specific
 *             ePaper configuration.
 *
 * @return None
 *
 * @attention Ensure the `room` parameter is valid and correctly configured. Passing a NULL
 * pointer or unsupported `epapertype` will result in no rendering and a debug log error.
 */
void render_firmware_info(float battery_voltage) {
    // Single buffer for building and storing the final message
    char buffer[128];

    // Construct the firmware information string
    snprintf(buffer, sizeof(buffer), "%s %s %s, U=%.2fV",
             program_name, version, build_date, battery_voltage);

    // Render the constructed string on the ePaper
    switch (device_config_flash.data.epapertype) {
        case EPAPER_WAVESHARE_7IN5_V2:
            Paint_DrawString_EN(500, 464, buffer, &Font12, WHITE, BLACK);
            break;

        case EPAPER_WAVESHARE_4IN2_V2:
            Paint_DrawString_EN(150, 292, buffer, &Font8, WHITE, BLACK);
            break;

        case EPAPER_WAVESHARE_2IN9_V2:
            Paint_DrawString_EN(250, 284, buffer, &Font12, WHITE, BLACK);
            break;

        default:
            debug_log_with_color(COLOR_RED, "Unsupported ePaper type: %d\n", device_config_flash.data.epapertype);
            return;
    }
}

void epaper_finalize_and_powerdown(UBYTE* image) {
    if (image == NULL) {
        debug_log("No valid image buffer to display. Skipping ePaper operations.\n");
        return;
    }

    watchdog_update();

    // Display the final image based on the configured ePaper type
    #ifdef HIGH_VERBOSE_DEBUG
    debug_log("EPD_Display called for epaper type: %d\n", device_config_flash.data.epapertype);
    #endif

    switch (device_config_flash.data.epapertype) {
        case EPAPER_WAVESHARE_7IN5_V2:
#ifdef USE_CASE_WEATHERMAP
            EPD_7IN5_V2_Display_4Gray(image);
#else
            EPD_7IN5_V2_Display(image);
#endif
            break;

        case EPAPER_WAVESHARE_4IN2_V2:
#ifdef USE_CASE_WEATHERMAP
            EPD_4IN2_V2_Display_4Gray(image);
#else
            EPD_4IN2_V2_Display(image);
#endif
            break;

        case EPAPER_WAVESHARE_2IN9_V2:
            EPD_2IN9_V2_Display(image);
            break;

        default:
            debug_log_with_color(COLOR_RED, "Unsupported ePaper type: %d\n", device_config_flash.data.epapertype);
            free(image);
            return;
    }

    // Free allocated memory for the image
    free(image);
    image = NULL;
    watchdog_update();

    // Put the e-Paper display into sleep mode based on the type
    #ifdef HIGH_VERBOSE_DEBUG
    debug_log("Entering ePaper sleep mode for type: %d\n", device_config_flash.data.epapertype);
    #endif

    switch (device_config_flash.data.epapertype) {
        case EPAPER_WAVESHARE_7IN5_V2:
            EPD_7IN5_V2_Sleep();
            break;

        case EPAPER_WAVESHARE_4IN2_V2:
            EPD_4IN2_V2_Sleep();
            break;

        case EPAPER_WAVESHARE_2IN9_V2:
            EPD_2IN9_V2_Sleep();
            break;

        default:
            debug_log_with_color(COLOR_RED, "Unsupported ePaper type during sleep: %d\n", device_config_flash.data.epapertype);
            return;
    }

    // Short delay to ensure the sleep command is processed
    DEV_Delay_ms(200);

    // Proceed with complete power-off sequence
    #ifdef HIGH_VERBOSE_DEBUG
    debug_log("Shutting down the ePaper module...\n");
    #endif
    DEV_Module_Exit();
    watchdog_update();
}

/**
 * @brief Determines if Wi-Fi communication is required based on RoomConfig and pushbutton state.
 *
 * This function evaluates the current RoomConfig and pushbutton state to decide
 * if Wi-Fi data retrieval is necessary for rendering the ePaper content.
 *
 * @param pushbutton The current pushbutton state (bitwise representation of pressed buttons).
 * @param room_config Pointer to the RoomConfig structure.
 * @return true if Wi-Fi communication is needed; false otherwise.
 */
bool is_wifi_required(int pushbutton) {
    // Example conditions for requiring Wi-Fi:
    // - Default page (pushbutton 0) generally requires Wi-Fi to display live data.
    // - Specific RoomConfig types might always require Wi-Fi (e.g., conference rooms).
    // - Certain pushbutton states (e.g., displaying device parameters) might not need Wi-Fi.

    if (pushbutton == 0) {
        // Default page typically needs live data
        debug_log("Wi-Fi required: Default page 0.\n");
        return true;
    }

    if (device_config_flash.data.type == ROOM_TYPE_CONFERENCE) {
        // Example for future use: Conference rooms always require Wi-Fi for live updates
        return true;
    }

    if (pushbutton == 1) {
        debug_log("Wi-Fi not required: Page 1, static information page, videoconference.\n");
        return false;
    }

    if (pushbutton == 2) {
        debug_log("Wi-Fi not required: Page 2, static information page, unviversal decision maker.\n");
        return false;
    }

    if (pushbutton == 3) {
        debug_log("Wi-Fi not required: Page 3, static information page.\n");
        return false;
    }    // Default to requiring Wi-Fi unless explicitly exempted

    if (pushbutton == 4) {
        debug_log("Wi-Fi not required: Page 4, static information page.\n");
        return false;
    }    // Default to requiring Wi-Fi unless explicitly exempted
    return true;
}
/**
 * @brief Renders the server error page on the ePaper display.
 *
 * Displays an error message indicating that the server is unreachable,
 * even though the Wi-Fi connection is successful. Includes the current
 * time from the RTC and additional diagnostic information.
 *
 * @param room Pointer to the RoomConfig structure for ePaper configuration.
 * @param clock Pointer to the ds3231_t structure for accessing RTC data.
 * @param image_buffer Image buffer used for subimage rendering.
 */
void render_page_server_error(ds3231_t* clock, UBYTE* image_buffer) {
    char buffer[256]; // Buffer for formatted strings
    ds3231_data_t ds3231_data;

    // Read the current time from the RTC
    ds3231_read_current_time(clock, &ds3231_data);

    // Clear the ePaper display
    Paint_Clear(WHITE);
    const char* server_error_msg = "Unable to reach the server";

    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {

        // Display the default logo
        DrawSubImage(image_buffer, &inki_octopus_100_95, 270, 5);

        // Display the room name
        Paint_DrawString_EN(70, 60, device_config_flash.data.roomname, &font_ubuntu_mono_28pt_bold, WHITE, BLACK);

        // Render error message
        Paint_DrawString_EN(50, 200, "Server Error!", &font_ubuntu_mono_22pt_bold, WHITE, BLACK);
        Paint_DrawString_EN(50, 280, server_error_msg, &font_ubuntu_mono_16pt, WHITE, BLACK);

        // Display a tip or diagnostic message
        Paint_DrawString_EN(50, 350, "Please check the server status.", &font_ubuntu_mono_12pt, WHITE, BLACK);

        format_rtc_time(&ds3231_data, buffer, sizeof(buffer));
        Paint_DrawString_EN(40, 420, buffer, &font_ubuntu_mono_10pt, WHITE, BLACK);

    } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {

        // Display room name & logo

        // Display room name & logo
        const char* name = device_config_flash.data.roomname;

        Paint_DrawString_EN(20, 40, name, &font_ubuntu_mono_18pt_bold,  WHITE, BLACK);
        // Paint_DrawString_EN(20, 40, device_config_flash.data.roomname, &font_ubuntu_mono_18pt_bold, WHITE, BLACK);

        if (!draw_flash_logo(image_buffer, 285, 10)) {
            DrawSubImage(image_buffer, &inki_octopus_100_95, 280, 15);
        }

        // Render error message
        Paint_DrawString_EN(20, 120, "Server Error!", &font_ubuntu_mono_12pt_bold, WHITE, BLACK);
        Paint_DrawString_EN(20, 180, server_error_msg, &font_ubuntu_mono_8pt, WHITE, BLACK);

        format_rtc_time(&ds3231_data, buffer, sizeof(buffer));
        Paint_DrawString_EN(20, 260, buffer, &font_ubuntu_mono_8pt, WHITE, BLACK);

    } else {
        debug_log_with_color(COLOR_RED, "Unsupported ePaper type in render_page_server_error: %d\n", device_config_flash.data.epapertype);
    }

    // Log debug information
    debug_log_with_color(COLOR_RED, "Server error page rendered.\n");
}


/**
 * @brief Renders the Wi-Fi error page on the ePaper display.
 *
 * Displays an error message indicating that the device was unable to establish
 * a Wi-Fi connection. Includes the current time from the RTC and additional
 * diagnostic information.
 *
 * @param room Pointer to the RoomConfig structure for ePaper configuration.
 * @param clock Pointer to the ds3231_t structure for accessing RTC data.
 */
void render_page_wifi_error(ds3231_t* clock, UBYTE* image_buffer) {
    char buffer[256]; // Buffer for formatted strings
    ds3231_data_t ds3231_data;

    // Read the current time from the RTC
    ds3231_read_current_time(clock, &ds3231_data);

    // Clear the ePaper display
    Paint_Clear(WHITE);
    const char* wifi_error_msg = "Unable to connect to WiFi";

    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {

        // Display the default logo
        DrawSubImage(image_buffer, &inki_octopus_100_95, 680, 25);

        // Display the room name
        Paint_DrawString_EN(70, 60, device_config_flash.data.roomname, &font_ubuntu_mono_28pt_bold, WHITE, BLACK);

        // Render error message
        Paint_DrawString_EN(50, 200, "WiFi Error!", &font_ubuntu_mono_22pt_bold, WHITE, BLACK);
        Paint_DrawString_EN(50, 280, wifi_error_msg, &font_ubuntu_mono_16pt, WHITE, BLACK);

        // Display a tip or diagnostic message
        Paint_DrawString_EN(50, 350, "Please check the WiFi settings.", &font_ubuntu_mono_12pt, WHITE, BLACK);

        format_rtc_time(&ds3231_data, buffer, sizeof(buffer));
        Paint_DrawString_EN(40, 420, buffer, &font_ubuntu_mono_10pt, WHITE, BLACK);

    } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {

        // Display room name & logo
        Paint_DrawString_EN(20, 40, device_config_flash.data.roomname, &font_ubuntu_mono_18pt_bold,  WHITE, BLACK);
        DrawSubImage(image_buffer, &inki_octopus_100_95, 270, 5);

        // Render error message
        Paint_DrawString_EN(20, 120, "WiFi Error!", &font_ubuntu_mono_12pt_bold, WHITE, BLACK);
        Paint_DrawString_EN(20, 180, wifi_error_msg, &font_ubuntu_mono_8pt, WHITE, BLACK);

        // Display a tip or diagnostic message
        // Paint_DrawString_EN(20, 190, "Please check the Wi-Fi settings.", &font_ubuntu_mono_9pt, WHITE, BLACK);

        format_rtc_time(&ds3231_data, buffer, sizeof(buffer));
        Paint_DrawString_EN(20, 260, buffer, &font_ubuntu_mono_8pt, WHITE, BLACK);
    }else {
        debug_log_with_color(COLOR_RED, "Unsupported ePaper type in render_page_wifi_error: %d\n", device_config_flash.data.epapertype);
    }

    // Log debug information
    debug_log_with_color(COLOR_RED, "Wi-Fi error page rendered.\n");
}

void render_page_wifi_setup(UBYTE* image) {
    int x_offset = 0;
    int y_offset = 0;
#ifdef USE_CASE_HISTORIAN
    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {
        x_offset = (EPD_7IN5_V2_WIDTH - EPD_4IN2_V2_WIDTH) / 2;
        y_offset = (EPD_7IN5_V2_HEIGHT - EPD_4IN2_V2_HEIGHT) / 2;
    }
#endif

    if (!draw_flash_logo(image, 290 + x_offset, 10 + y_offset)) {
        DrawSubImage(image, &inki_octopus_100_95, 290 + x_offset, 15 + y_offset);
    }

    Paint_DrawString_EN(20 + x_offset, 20 + y_offset, "WIFI Setup Mode", &font_ubuntu_mono_11pt, WHITE, BLACK);
    Paint_DrawString_EN(20 + x_offset, 80 + y_offset, "Connect to ", &font_ubuntu_mono_10pt, WHITE, BLACK);
    Paint_DrawString_EN(60 + x_offset, 130 + y_offset, "inki-setup", &font_ubuntu_mono_12pt_bold, WHITE, BLACK);

    Paint_DrawString_EN(20 + x_offset, 180 + y_offset, "Go to ", &font_ubuntu_mono_10pt, WHITE, BLACK);
    Paint_DrawString_EN(30 + x_offset, 230 + y_offset, "http://192.168.4.1 ", &font_ubuntu_mono_12pt_bold, WHITE, BLACK);

    // char line1[64];
    // char line2[64];
    // snprintf(line1, sizeof(line1), "SSID: %s", wifi_config_flash.ssid);
    // snprintf(line2, sizeof(line2), "PWD:  %s", wifi_config_flash.password);
    // Paint_DrawString_EN(20 + x_offset, 240 + y_offset, line1, &font_ubuntu_mono_8pt, WHITE, BLACK);
    // Paint_DrawString_EN(20 + x_offset, 250 + y_offset, line2, &font_ubuntu_mono_8pt, WHITE, BLACK);
}

static const uint8_t dhcp_offer_template[] = {
    0x02, 0x01, 0x06, 0x00,                  // BOOTP: op, htype, hlen, hops
    0x00, 0x00, 0x00, 0x00,                  // XID (Transaction ID)
    0x00, 0x00, 0x00, 0x00,                  // SECS, FLAGS
    0, 0, 0, 0,                              // CIADDR (Client IP)
    192, 168, 4, 100,                        // YIADDR (Your IP)
    192, 168, 4, 1,                          // SIADDR (Server IP)
    0x00, 0x00, 0x00, 0x00,                  // GIADDR (Gateway IP)
    // CHADDR (Client HW addr)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // CHADDR padding
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // SNAME (64 bytes)
    [44] = 0,  // ab hier (Offset 44) bis 107:
    [107] = 0,
    // FILE (128 bytes)
    [108] = 0, // ab hier bis 235
    [235] = 0,
    // MAGIC COOKIE
    99, 130, 83, 99,
    // DHCP Options
    53, 1, 2,                                // DHCP Message Type: Offer
    54, 4, 192, 168, 4, 1,                   // Server Identifier
    51, 4, 0x00, 0x01, 0x51, 0x80,           // Lease time = 86400
    58, 4, 0x00, 0x00, 0x01, 0x2C,           // Renewal (T1) = 300s
    59, 4, 0x00, 0x00, 0x01, 0xE0,           // Rebinding (T2) = 480s
    1, 4, 255, 255, 255, 0,                  // Subnet mask
    3, 4, 192, 168, 4, 1,                    // Router
    6, 4, 192, 168, 4, 1,                    // DNS
    255                                     // End option
};

static const uint8_t dhcp_ack_template[] = {
    0x02, 0x01, 0x06, 0x00,                  // BOOTP: op, htype, hlen, hops
    0x00, 0x00, 0x00, 0x00,                  // XID (Transaction ID)
    0x00, 0x00, 0x00, 0x00,                  // SECS, FLAGS
    0, 0, 0, 0,                              // CIADDR (Client IP)
    192, 168, 4, 100,                        // YIADDR (Your IP)
    192, 168, 4, 1,                          // SIADDR (Server IP)
    0x00, 0x00, 0x00, 0x00,                  // GIADDR (Gateway IP)
    // CHADDR (Client HW addr)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // CHADDR padding
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // SNAME (64 bytes)
    [44] = 0,  // Offset identisch wie oben
    [107] = 0,
    // FILE (128 bytes)
    [108] = 0,
    [235] = 0,
    // MAGIC COOKIE
    99, 130, 83, 99,
    // DHCP Options
    53, 1, 5,                                // DHCP Message Type: ACK
    54, 4, 192, 168, 4, 1,                   // Server Identifier
    51, 4, 0x00, 0x01, 0x51, 0x80,           // Lease time = 86400
    58, 4, 0x00, 0x00, 0x01, 0x2C,           // Renewal (T1) = 300s
    59, 4, 0x00, 0x00, 0x01, 0xE0,           // Rebinding (T2) = 480s
    1, 4, 255, 255, 255, 0,                  // Subnet mask
    3, 4, 192, 168, 4, 1,                    // Router
    6, 4, 192, 168, 4, 1,                    // DNS
    255                                     // End option
};

static void dhcp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                         const ip_addr_t *addr, u16_t port) {
    if (!p || p->len < 240) {
        if (p) pbuf_free(p);
        return;
    }

    const uint8_t *request = (const uint8_t *)p->payload;

    // DHCP Message Type (Option 53) auslesen
    uint8_t msg_type = 0;
    for (int i = 240; i < p->len - 2; i++) {
        if (request[i] == 53 && request[i + 1] == 1) {
            msg_type = request[i + 2];
            break;
        }
    }

    // DHCP Offer oder ACK auswählen
    const uint8_t *template = NULL;
    size_t template_len = 0;

    switch (msg_type) {
        case 1: // DHCP Discover
            template = dhcp_offer_template;
            template_len = sizeof(dhcp_offer_template);
            break;
        case 3: // DHCP Request
            template = dhcp_ack_template;
            template_len = sizeof(dhcp_ack_template);
            break;
        default:
            pbuf_free(p);
            return;
    }

    // DHCP Antwort vorbereiten
    uint8_t response[300] = {0};
    memcpy(response, template, template_len);

    // XID (Transaction ID) und CHADDR (Client MAC) kopieren
    memcpy(response + 4, request + 4, 4);     // XID
    memcpy(response + 28, request + 28, 16);  // CHADDR

    // Antwort-Puffer erzeugen
    struct pbuf *resp_buf = pbuf_alloc(PBUF_TRANSPORT, template_len, PBUF_RAM);
    if (!resp_buf) {
        pbuf_free(p);
        return;
    }

    memcpy(resp_buf->payload, response, template_len);
    udp_sendto(pcb, resp_buf, addr, port);

    // Aufräumen
    pbuf_free(resp_buf);
    pbuf_free(p);
}

void start_dhcp_server(void) {
    struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) return;

    if (udp_bind(pcb, IP_ADDR_ANY, 67) != ERR_OK) {
        udp_remove(pcb);
        return;
    }

    udp_recv(pcb, dhcp_recv_cb, NULL);
}

void enter_wifi_setup_mode(ds3231_t* clock) {
    static web_submission_t latest_submission;
    static bool submission_received = false;

    UBYTE* BlackImage = init_epaper();
    if (BlackImage != NULL) {
        render_page_wifi_setup(BlackImage);
        epaper_finalize_and_powerdown(BlackImage);
    }

    debug_log_with_color(COLOR_GREEN, "WiFi setup mode: initializing...\n");

    if (cyw43_arch_init_with_country(CYW43_COUNTRY_GERMANY)) {
        debug_log_with_color(COLOR_RED, "CYW43 initialization failed.\n");
        transmit_debug_logs();
        set_alarmclock_and_powerdown(clock);
        exit(0);
    }

    const char* ssid = "inki-setup";
    const char* password = "12345678";

    cyw43_arch_enable_ap_mode(ssid, password, CYW43_AUTH_WPA2_AES_PSK);

    ip4_addr_t ip, netmask, gw;
    IP4_ADDR(&ip, 192, 168, 4, 1);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 4, 1);

    absolute_time_t shutdown_time = make_timeout_time_ms(WIFI_SETUP_TIMEOUT_MS);
    webserver_set_shutdown_time(shutdown_time);

    start_setup_webserver();
    start_dhcp_server();

    if (cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_AP, mac_address) != 0) {
        debug_log_with_color(COLOR_RED, "Failed to retrieve MAC address.\n");
    } else {
        debug_log_with_color(COLOR_BOLD_GREEN,
                             "AP MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                             mac_address[0], mac_address[1], mac_address[2],
                             mac_address[3], mac_address[4], mac_address[5]);
    }

    debug_log_with_color(COLOR_GREEN, "Access Point active: SSID = %s, IP = 192.168.4.1\n", ssid);

    // LED behavior while web interface is active
#if LED_MORSE_ENABLED
    // Start Morse engine with default message
    morse_set_unit_ms(LED_MORSE_UNIT_MS);
    morse_set_message("INKI");
    morse_set_enabled(false);
#else
    // Morse disabled: keep LEDs ON solid during setup
#if LED_USE_EXT
    ext_led_on();
#endif
#endif

    absolute_time_t last_watchdog_feed = get_absolute_time();

    while (true) {
        bool firmware_upload = webserver_firmware_upload_active();

        cyw43_arch_poll();

        if (firmware_upload) {
            watchdog_update();
        } else {
            sleep_ms(50);

            // Drive Morse engine (non-blocking)
#if LED_MORSE_ENABLED
            morse_tick();
#endif
        }

        if (absolute_time_diff_us(get_absolute_time(), last_watchdog_feed) < -2000000) {
            watchdog_update();
            last_watchdog_feed = get_absolute_time();
        }

        if (absolute_time_diff_us(get_absolute_time(), shutdown_time) < 0) {
            debug_log_with_color(COLOR_BOLD_YELLOW, "Setup timeout erreicht – Gerät wird heruntergefahren.\n");
            cyw43_arch_deinit();
            transmit_debug_logs();
            set_alarmclock_and_powerdown(clock);
            exit(0);
        }
    }
}

void print_firmware_slots_status(void) {
    // Show which firmware is currently running
    const char* active_info = get_active_firmware_slot_info();
    debug_log_with_color(COLOR_BOLD_GREEN, "Running firmware from: %s\n", active_info);

    char build0[16] = {0}, version0[32] = {0};
    char build1[16] = {0}, version1[32] = {0};
    uint32_t size0 = 0, size1 = 0;
    uint32_t crc0 = 0, crc1 = 0;
    uint8_t slot_index0 = 0, slot_index1 = 0;
    uint8_t valid0 = 0, valid1 = 0;

    bool has0 = get_firmware_slot_info(0, build0, version0, &size0, &crc0, &slot_index0, &valid0);
    bool has1 = get_firmware_slot_info(1, build1, version1, &size1, &crc1, &slot_index1, &valid1);

    const char* color_slot0 = COLOR_YELLOW;
    const char* color_slot1 = COLOR_YELLOW;

    // Check if current slot matches Slot 0 or Slot 1
    if (strstr(active_info, "SLOT_0")) {
        color_slot0 = COLOR_BOLD_YELLOW;
    } else if (strstr(active_info, "SLOT_1")) {
        color_slot1 = COLOR_BOLD_YELLOW;
    }

    if (has0) {
        debug_log_with_color(color_slot0, "Slot 0: Version %s, Build %s, Size %u Bytes\n", version0, build0, size0);
    } else {
        debug_log_with_color(color_slot0, "Slot 0: (no valid firmware)\n");
    }

    if (has1) {
        debug_log_with_color(color_slot1, "Slot 1: Version %s, Build %s, Size %u Bytes\n", version1, build1, size1);
    } else {
        debug_log_with_color(color_slot1, "Slot 1: (no valid firmware)\n");
    }
}

bool wait_for_usb_connection(uint32_t timeout_ms) {
    const uint32_t step_ms = 10;
    uint32_t waited = 0;

    while (!stdio_usb_connected()) {
        sleep_ms(step_ms);
        waited += step_ms;
        if (waited >= timeout_ms) {
            return false;  // Timeout
        }
    }
    return true;  // Verbunden
}

int main(void)
{
    // Set debug mode (real-time, buffered, or both)
    // set_debug_mode(DEBUG_BUFFERED);
    set_debug_mode(DEBUG_REALTIME);

    debug_log_with_color(COLOR_GREEN, "hold power\n");
    hold_power();  // Hold power state of the circuit

    stdio_init_all();     // Initialize standard I/O

    // Initialize HTTP client + TLS trust store (for WEATHERMAP TLS)
    http_client_init();
    if (wait_for_usb_connection(2500)) { // only used for debugging
        printf("USB connected\n");
    } else {
        printf("USB timeout\n");
    }

    debug_log_with_color(COLOR_BOLD_GREEN, "System initializing - inki-"
#ifdef USE_CASE_SEATSURFING
                          "seatsurfing"
#elif defined(USE_CASE_HISTORIAN)
                          "historian"
#elif defined(USE_CASE_HOMEMATIC)
                          "homematic"
#elif defined(USE_CASE_WEATHERMAP)
                          "weathermap"
#else
                          "unknown"
#endif
                          "\n");

    // Initialize external LED on GP16 and turn on to indicate power
    ext_led_init(16);
    ext_led_on();

    print_firmware_slots_status();

    debug_log_with_color(COLOR_GREEN, "watchdog_enable\n");
    watchdog_enable(device_config_flash.data.watchdog_time, 0);

    debug_log_with_color(COLOR_GREEN, "ADC read\n");
    float battery_voltage = read_battery_voltage(device_config_flash.data.conversion_factor);

    debug_log_with_color(COLOR_GREEN, "init real time clock DS3231\n");
    ds3231 = init_clock(); // Initialize clock

    debug_log_with_color(COLOR_GREEN, "start setup_and_read_pushbuttons\n");
    setup_and_read_pushbuttons();     // Initialize pushbuttons and read their state

    // pushbutton = 7; // use for debugging

#ifdef USE_CASE_HISTORIAN
    if (pushbutton == 4) {
        debug_log_with_color(COLOR_BOLD_YELLOW, "Historian: launching web interface (page 4)\n");
        enter_wifi_setup_mode(&ds3231);
    }
#endif

    // Enter WiFi setup mode if all three buttons are held
    if (pushbutton == 7) {
        debug_log_with_color(COLOR_BOLD_YELLOW, "WiFi setup mode activated (pushbutton 7)\n");
        enter_wifi_setup_mode(&ds3231);  // Launch the WiFi configuration access point and webserver
     //   return 0;  // The device will shut down inside setup mode (after timeout or user action)
    }

    // Emergency mode: if no ePaper configured, go directly to WiFi setup.
    if (device_config_flash.data.epapertype == EPAPER_NONE) {
        debug_log_with_color(COLOR_BOLD_YELLOW, "No ePaper configured — entering WiFi setup mode\n");
        enter_wifi_setup_mode(&ds3231);
    }

    debug_log_with_color(COLOR_GREEN, "server_communication\n");
    WifiResult wifi_result = WIFI_NOT_REQUIRED;

    if (is_wifi_required(pushbutton)) {
        // Use-case specific Wi-Fi data path
#if defined(USE_CASE_HISTORIAN)
        set_data_callback(historian_data_received, NULL);
        wifi_result = wifi_server_communication(battery_voltage);
#elif defined(USE_CASE_SEATSURFING)
        set_data_callback(seatsurfing_data_received, NULL);
        wifi_result = wifi_server_communication(battery_voltage);
#elif defined(USE_CASE_HOMEMATIC)
        set_data_callback(homematic_data_received, NULL);
        wifi_result = wifi_server_communication(battery_voltage);
#elif defined(USE_CASE_WEATHERMAP)
        // GEODATA: run one-shot TLS fetch (radar preferred, basemap fallback)
        extern void geodata_fetch(void);
        geodata_fetch();
        wifi_result = WIFI_SUCCESS;
#endif
    }

    UBYTE* BlackImage = init_epaper();
    if (BlackImage == NULL) {
        debug_log_with_color(COLOR_RED, "ePaper init or buffer allocation failed.\n");
        // If no ePaper is configured or init failed, fall back to WiFi setup to allow configuration.
        debug_log_with_color(COLOR_BOLD_YELLOW, "Falling back to WiFi setup mode\n");
        enter_wifi_setup_mode(&ds3231);
    }

    debug_log_with_color(COLOR_GREEN, "render_page\n");
    // Handle Wi-Fi and server errors with specific pages
    if (wifi_result == WIFI_ERROR_CONNECTION) {
        render_page_wifi_error(&ds3231, BlackImage); // Display Wi-Fi error page
    } else if (wifi_result == WIFI_ERROR_SERVER) {
        render_page_server_error(&ds3231,BlackImage); // Display server error page
    } else {
        render_page(pushbutton, &ds3231, BlackImage, battery_voltage); // Render normal page
    }

    if (pushbutton != 4) {
        render_firmware_info(battery_voltage);
    }

    debug_log_with_color(COLOR_GREEN, "epaper_finalize_and_powerdown (display epaper page)...\n");
    epaper_finalize_and_powerdown(BlackImage);

    // Transmit logs before shutdown
    debug_log_with_color(COLOR_BOLD_GREEN, "...System shutting down.  \n");
    transmit_debug_logs();

    set_alarmclock_and_powerdown(&ds3231);

//any code behind this should never be reached! 

    while (true)
    {
        sleep_ms(500);
    }
    return 0;
}
