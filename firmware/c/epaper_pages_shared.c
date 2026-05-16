#include "epaper_pages_shared.h"
#include "GUI_Paint.h"
#include "ImageResources.h"
#include "epaper.h"
#define LOG_MODULE LOG_MOD_EPAPER
#include "debug.h"
#include "epaper_render.h"
#include "flash.h"
#include "fonts.h"
#include "pico/rand.h"
#include "rtc.h"
#include "sensors.h"
#include "st25_io.h"
#include "use_case.h"
#include "wifi.h"
#include <stdio.h>
#include <string.h>

// --- NFC buffers (owned by boot_input.c, filled during boot) ---
extern char nfc_text_buf[];
extern uint8_t nfc_image_buf[];

// --- Layout arrays ---

static const layout_elem_t dnd_layout_75[] = {
    {ELEM_LOGO, 690, 10, .image = &inki_octopus_100_95},
    {ELEM_VAR, 50, 60, &font_ubuntu_mono_24pt_bold, .var_index = 0},
    {ELEM_TEXT, 80, 180, &font_ubuntu_mono_28pt_bold, .text = "Videoconference"},
    {ELEM_TEXT, 80, 270, &font_ubuntu_mono_20pt_bold, .text = "Please do not disturb"},
    {ELEM_VAR, 80, 340, &font_ubuntu_mono_14pt, .var_index = 1},
};

static const layout_elem_t dnd_layout_42[] = {
    {ELEM_VAR, 20, 40, &font_ubuntu_mono_18pt_bold, .var_index = 0},
    {ELEM_LOGO, 290, 10, .image = &inki_octopus_100_95},
    {ELEM_TEXT, 50, 120, &font_ubuntu_mono_14pt_bold, .text = "Please,"},
    {ELEM_TEXT, 50, 170, &font_ubuntu_mono_14pt_bold, .text = "Do Not Disturb!"},
    {ELEM_VAR, 70, 240, &font_ubuntu_mono_10pt, .var_index = 1},
    {ELEM_TEXT, 8, 292, &Font8, .text = "1"},
};

static const layout_elem_t udm_layout_75[] = {
    {ELEM_LOGO, 680, 20, .image = &inki_octopus_100_95},
    {ELEM_VAR, 40, 50, &font_ubuntu_mono_28pt_bold, .var_index = 0},
    {ELEM_TEXT, 25, 180, &font_ubuntu_mono_16pt, .text = "Universal Decision Maker says:"},
    {ELEM_VAR, 295, 280, &font_ubuntu_mono_36pt_bold, .var_index = 1},
};

static const layout_elem_t udm_layout_42[] = {
    {ELEM_LOGO, 290, 10, .image = &inki_octopus_100_95},
    {ELEM_TEXT, 25, 40, &font_ubuntu_mono_11pt, .text = "Universal "},
    {ELEM_TEXT, 25, 70, &font_ubuntu_mono_11pt, .text = "Decision "},
    {ELEM_TEXT, 25, 100, &font_ubuntu_mono_11pt, .text = "Maker says:"},
    {ELEM_VAR, 155, 180, &font_ubuntu_mono_22pt_bold, .var_index = 1},
    {ELEM_TEXT, 8, 292, &Font8, .text = "2"},
};

static const layout_elem_t nfc_text_layout_75[] = {
    {ELEM_VAR, 30, 30, &font_ubuntu_mono_16pt, .var_index = 0},
};

static const layout_elem_t nfc_text_layout_42[] = {
    {ELEM_VAR, 47, 134, &font_ubuntu_mono_12pt_bold, .var_index = 0},
};

static const layout_elem_t info_layout_75[] = {
    {ELEM_SUBIMAGE, 270, 5, .image = &inki_octopus_100_95},
    {ELEM_VAR, 70, 60, &font_ubuntu_mono_28pt_bold, .var_index = 0},
};

static const layout_elem_t info_layout_42[] = {
    {ELEM_LOGO, 290, 10, .image = &inki_octopus_100_95},
    {ELEM_VAR, 10, 20, &font_ubuntu_mono_14pt_bold, .var_index = 0},
    {ELEM_VAR, 10, 70, &font_ubuntu_mono_6pt, .var_index = 1},
    {ELEM_VAR, 10, 90, &font_ubuntu_mono_6pt, .var_index = 2},
    {ELEM_VAR, 10, 110, &font_ubuntu_mono_6pt, .var_index = 3},
    {ELEM_VAR, 10, 130, &font_ubuntu_mono_6pt, .var_index = 4},
    {ELEM_VAR, 10, 150, &font_ubuntu_mono_6pt, .var_index = 5},
    {ELEM_VAR, 10, 170, &font_ubuntu_mono_6pt, .var_index = 6},
    {ELEM_VAR, 10, 190, &font_ubuntu_mono_6pt, .var_index = 7},
    {ELEM_VAR, 10, 210, &font_ubuntu_mono_6pt, .var_index = 8},
    {ELEM_VAR, 10, 230, &font_ubuntu_mono_6pt, .var_index = 9},
    {ELEM_VAR, 10, 250, &font_ubuntu_mono_6pt, .var_index = 10},
    {ELEM_TEXT, 8, 292, &Font8, .text = "3"},
};

static const layout_elem_t error_layout_75[] = {
    {ELEM_LOGO, 680, 20, .image = &inki_octopus_100_95},
    {ELEM_VAR, 70, 60, &font_ubuntu_mono_28pt_bold, .var_index = 0},
    {ELEM_VAR, 50, 200, &font_ubuntu_mono_22pt_bold, .var_index = 1},
    {ELEM_VAR, 50, 280, &font_ubuntu_mono_16pt, .var_index = 2},
    {ELEM_VAR, 50, 350, &font_ubuntu_mono_12pt, .var_index = 3},
    {ELEM_VAR, 40, 420, &font_ubuntu_mono_10pt, .var_index = 4},
};

static const layout_elem_t error_layout_42[] = {
    {ELEM_LOGO, 280, 10, .image = &inki_octopus_100_95},
    {ELEM_VAR, 20, 40, &font_ubuntu_mono_18pt_bold, .var_index = 0},
    {ELEM_VAR, 20, 120, &font_ubuntu_mono_12pt_bold, .var_index = 1},
    {ELEM_VAR, 20, 180, &font_ubuntu_mono_8pt, .var_index = 2},
    {ELEM_VAR, 20, 215, &font_ubuntu_mono_8pt, .var_index = 3},
    {ELEM_VAR, 20, 260, &font_ubuntu_mono_8pt, .var_index = 4},
};

static const layout_elem_t wifi_setup_layout_42[] = {
    {ELEM_LOGO, 290, 10, .image = &inki_octopus_100_95},
    {ELEM_TEXT, 20, 20, &font_ubuntu_mono_11pt, .text = "WIFI Setup Mode"},
    {ELEM_TEXT, 20, 80, &font_ubuntu_mono_10pt, .text = "Connect to "},
    {ELEM_TEXT, 47, 130, &font_ubuntu_mono_12pt_bold, .text = "http://inki-setup"},
    {ELEM_TEXT, 20, 180, &font_ubuntu_mono_10pt, .text = "or"},
    {ELEM_TEXT, 38, 230, &font_ubuntu_mono_12pt_bold, .text = "http://192.168.4.1"},
};

static const layout_elem_t wifi_setup_layout_75[] = {
    {ELEM_LOGO, 490, 100, .image = &inki_octopus_100_95},
    {ELEM_TEXT, 220, 110, &font_ubuntu_mono_11pt, .text = "WIFI Setup Mode"},
    {ELEM_TEXT, 220, 170, &font_ubuntu_mono_10pt, .text = "Connect to "},
    {ELEM_TEXT, 247, 220, &font_ubuntu_mono_12pt_bold, .text = "http://inki-setup"},
    {ELEM_TEXT, 220, 270, &font_ubuntu_mono_10pt, .text = "or"},
    {ELEM_TEXT, 238, 320, &font_ubuntu_mono_12pt_bold, .text = "http://192.168.4.1"},
};

// --- Render functions ---

void render_page_placeholder(uint8_t *image_buffer, const char *title) {
    Paint_Clear(WHITE);

    rtc_time_t rtc_data;
    rtc_read_time(&rtc_data);
    char datetime_buf[64];
    rtc_format_time(&rtc_data, datetime_buf, sizeof(datetime_buf));

    const char *msg = "Not assigned";

    const sFONT *title_font = &font_ubuntu_mono_14pt_bold;
    const sFONT *info_font = &font_ubuntu_mono_12pt;
    const sFONT *datetime_font = &font_ubuntu_mono_10pt;

    const int epd_width = epaper_get_width();
    const int epd_height = epaper_get_height();

    int logo_x = epd_width - inki_octopus_100_95.width - 10;
    if (logo_x < 0)
        logo_x = 0;
    if (!epaper_draw_custom_logo(image_buffer, logo_x, 10))
        epaper_draw_subimage(image_buffer, &inki_octopus_100_95, logo_x, 10);

    int center_x = epd_width / 2;
    int center_y = epd_height / 2;

    int title_x = center_x - (int)strlen(title) * title_font->Width / 2;
    int title_y = center_y - 40;
    Paint_DrawString_EN(title_x, title_y, title, (sFONT *)title_font, WHITE, BLACK);

    int msg_x = center_x - (int)strlen(msg) * info_font->Width / 2;
    Paint_DrawString_EN(msg_x, title_y + title_font->Height + 8, msg, (sFONT *)info_font, WHITE,
                        BLACK);

    int datetime_y = epd_height - datetime_font->Height - 20;
    if (datetime_y < 0)
        datetime_y = 0;
    Paint_DrawString_EN(30, datetime_y, datetime_buf, (sFONT *)datetime_font, WHITE, BLACK);
}

static void render_page_placeholder_default(uint8_t *image_buffer) {
    render_page_placeholder(image_buffer, "inki");
}

void render_page_fallback(int page, uint8_t *image_buffer) {
    dlog("Unassigned page index: %d\n", page);
    epaper_draw_subimage(image_buffer, &inki_octopus_100_95, 270, 5);
}

static void render_page_dnd(uint8_t *image_buffer) {
    Paint_Clear(WHITE);

    rtc_time_t rtc_data;
    rtc_read_time(&rtc_data);
    char time_string[8];
    rtc_format_time_short(&rtc_data, time_string, sizeof(time_string));
    char start_buf[32];
    snprintf(start_buf, sizeof(start_buf), "Start: %s", time_string);

    const char *vars[] = {device_config_flash.data.roomname, start_buf};

    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {
        epaper_render_layout(image_buffer, dnd_layout_75,
                             sizeof(dnd_layout_75) / sizeof(dnd_layout_75[0]), vars);
    } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {
        epaper_render_layout(image_buffer, dnd_layout_42,
                             sizeof(dnd_layout_42) / sizeof(dnd_layout_42[0]), vars);
    } else {
        dlog("render_page_dnd is not supported for the configured ePaper type.\n");
        epaper_draw_custom_logo(image_buffer, 285, 10);
    }
}

static void render_page_decision_maker(uint8_t *image_buffer) {
    Paint_Clear(WHITE);

    const char *decision = (get_rand_32() % 2) ? "No!" : "Yes!";
    const char *vars[] = {device_config_flash.data.roomname, decision};

    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {
        epaper_render_layout(image_buffer, udm_layout_75,
                             sizeof(udm_layout_75) / sizeof(udm_layout_75[0]), vars);
    } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {
        epaper_render_layout(image_buffer, udm_layout_42,
                             sizeof(udm_layout_42) / sizeof(udm_layout_42[0]), vars);
    } else {
        dlog("render_page_decision_maker is not supported for the configured ePaper type.\n");
        epaper_draw_custom_logo(image_buffer, 285, 10);
    }
}

static void render_page_nfc_text(uint8_t *image_buffer) {
    Paint_Clear(WHITE);

    const char *text = nfc_text_buf;
    if (text[0] == '\0')
        text = "(no message)";

    const char *vars[] = {text};

    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {
        epaper_render_layout(image_buffer, nfc_text_layout_75,
                             sizeof(nfc_text_layout_75) / sizeof(nfc_text_layout_75[0]), vars);
    } else {
        epaper_render_layout(image_buffer, nfc_text_layout_42,
                             sizeof(nfc_text_layout_42) / sizeof(nfc_text_layout_42[0]), vars);
    }
    epaper_draw_firmware_info();
}

static void render_page_device_info(uint8_t *image_buffer) {
    Paint_Clear(WHITE);

    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {
        const char *vars[] = {device_config_flash.data.roomname};
        epaper_render_layout(image_buffer, info_layout_75,
                             sizeof(info_layout_75) / sizeof(info_layout_75[0]), vars);

    } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {
        char buf[12][64];

        snprintf(buf[1], sizeof(buf[1]), "ssid: %s", wifi_config_flash.ssid);
        snprintf(buf[2], sizeof(buf[2]), "wifi_reconnect_minutes: %i",
                 device_config_flash.data.wifi_reconnect_minutes);
        snprintf(buf[3], sizeof(buf[3]), "wifi_timeout: %i", device_config_flash.data.wifi_timeout);
        snprintf(buf[4], sizeof(buf[4]), "refresh_minutes: [%d,%d,%d,%d,%d,%d,%d,%d]",
                 device_config_flash.data.refresh_minutes_by_pushbutton[0],
                 device_config_flash.data.refresh_minutes_by_pushbutton[1],
                 device_config_flash.data.refresh_minutes_by_pushbutton[2],
                 device_config_flash.data.refresh_minutes_by_pushbutton[3],
                 device_config_flash.data.refresh_minutes_by_pushbutton[4],
                 device_config_flash.data.refresh_minutes_by_pushbutton[5],
                 device_config_flash.data.refresh_minutes_by_pushbutton[6],
                 device_config_flash.data.refresh_minutes_by_pushbutton[7]);

        rtc_time_t rtc_data;
        rtc_read_time(&rtc_data);
        snprintf(buf[5], sizeof(buf[5]), "RTC (raw): %02i:%02i, %s, %02i. %s %04i", rtc_data.hours,
                 rtc_data.minutes, rtc_day_name(rtc_data.day), rtc_data.date,
                 rtc_month_name(rtc_data.month), 2000 + rtc_data.year);

        char rtc_dst[64];
        rtc_format_time(&rtc_data, rtc_dst, sizeof(rtc_dst));
        snprintf(buf[6], sizeof(buf[6]), "RTC (DST): %s", rtc_dst);

        const uint8_t *mac = wifi_mac();
        snprintf(buf[7], sizeof(buf[7]), "MAC address: %02X:%02X:%02X:%02X:%02X:%02X", mac[0],
                 mac[1], mac[2], mac[3], mac[4], mac[5]);

        float coin_voltage = read_coin_cell_voltage(device_config_flash.data.conversion_factor);
        snprintf(buf[8], sizeof(buf[8]), "Vcc: %.3fV", epaper_get_battery_voltage());
        snprintf(buf[9], sizeof(buf[9]), "Vbat: %.3fV", coin_voltage);
        snprintf(buf[10], sizeof(buf[10]), "adc conv.: %.8f",
                 device_config_flash.data.conversion_factor);

        const char *vars[LAYOUT_MAX_VARS] = {
            device_config_flash.data.roomname,
            buf[1],
            buf[2],
            buf[3],
            buf[4],
            buf[5],
            buf[6],
            buf[7],
            buf[8],
            buf[9],
            buf[10],
        };
        epaper_render_layout(image_buffer, info_layout_42,
                             sizeof(info_layout_42) / sizeof(info_layout_42[0]), vars);
        epaper_draw_battery_icon(epaper_get_battery_voltage(), image_buffer, 330, 190);

    } else {
        dlog("render_page_device_info is not supported for the configured ePaper type.\n");
        epaper_draw_subimage(image_buffer, &inki_octopus_100_95, 270, 5);
    }
    epaper_draw_firmware_info();
}

void render_page_error(uint8_t *image_buffer, const char *title, const char *detail,
                       const char *tip) {
    Paint_Clear(WHITE);

    rtc_time_t rtc_data;
    rtc_read_time(&rtc_data);
    char timebuf[64];
    rtc_format_time(&rtc_data, timebuf, sizeof(timebuf));

    const char *vars[] = {device_config_flash.data.roomname, title, detail, tip, timebuf};

    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {
        epaper_render_layout(image_buffer, error_layout_75,
                             sizeof(error_layout_75) / sizeof(error_layout_75[0]), vars);
    } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {
        epaper_render_layout(image_buffer, error_layout_42,
                             sizeof(error_layout_42) / sizeof(error_layout_42[0]), vars);
    } else {
        dlog("Unsupported ePaper type in render_page_error: %d\n",
             device_config_flash.data.epapertype);
    }
}

void render_page_wifi_setup(uint8_t *image_buffer) {
    Paint_Clear(WHITE);

    if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) {
        epaper_render_layout(image_buffer, wifi_setup_layout_75,
                             sizeof(wifi_setup_layout_75) / sizeof(wifi_setup_layout_75[0]), NULL);
    } else if (device_config_flash.data.epapertype == EPAPER_WAVESHARE_4IN2_V2) {
        epaper_render_layout(image_buffer, wifi_setup_layout_42,
                             sizeof(wifi_setup_layout_42) / sizeof(wifi_setup_layout_42[0]), NULL);
    } else {
        dlog("render_page_wifi_setup is not supported for the configured ePaper type.\n");
    }
    epaper_draw_firmware_info();
}

static void render_page_nfc_image(uint8_t *image_buffer) {

    const bool is_75 = (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2);
    const int disp_w = is_75 ? 800 : 400;
    const int disp_h = is_75 ? 480 : 300;
    const int scale = is_75 ? 6 : 4; /* 6× → 372×372 on 7.5", 4× → 248×248 on 4.2" */

    const int img_w = (int)ST25_IMAGE_COLS;
    const int img_h = (int)ST25_IMAGE_ROWS;
    const int px_w = img_w * scale;
    const int px_h = img_h * scale;
    const int x0 = (disp_w - px_w) / 2;
    const int y0 = (disp_h - px_h) / 2;

    Paint_SelectImage(image_buffer);
    Paint_Clear(WHITE);

    for (int row = 0; row < img_h; row++) {
        for (int col = 0; col < img_w; col++) {
            const int bit_idx = row * img_w + col;
            const uint8_t byte = nfc_image_buf[bit_idx / 8];
            const bool black = (byte >> (7 - (bit_idx % 8))) & 1u;
            if (black) {
                const int sx = x0 + col * scale;
                const int sy = y0 + row * scale;
                Paint_DrawRectangle(sx, sy, sx + scale - 1, sy + scale, BLACK, DOT_PIXEL_1X1,
                                    DRAW_FILL_FULL);
            }
        }
    }
}

// --- Shared page_def_t constants ---

const page_def_t page_dnd = {"dnd", render_page_dnd, false};
const page_def_t page_decision_maker = {"decision_maker", render_page_decision_maker, false};
const page_def_t page_device_info = {"device_info", render_page_device_info, false};
const page_def_t page_nfc_text = {"nfc_text", render_page_nfc_text, false};
const page_def_t page_nfc_image = {"nfc_image", render_page_nfc_image, false};
const page_def_t page_wifi_setup = {"wifi_setup", render_page_wifi_setup, false};
const page_def_t page_placeholder = {"placeholder", render_page_placeholder_default, false};
