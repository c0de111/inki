#include "web_config.h"
#include "config.h"
#define LOG_MODULE LOG_MOD_WEBSERVER
#include "debug.h"
#include "flash.h"
#include "pico/time.h"
#include "weathermap/weathermap_flash.h"
#include "webserver.h"
#include "webserver_scaffold.h"
#include "webserver_utils.h"
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void wm_render_config_page(char *buf, size_t sz, const char *msg) {
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    weathermap_config_t cfg;
    if (!load_weathermap_config(&cfg))
        init_weathermap_config(&cfg);

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

    char lat_buf[32], lon_buf[32], half_buf[32];

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

    int n = scaffold_page_open(buf, sz, "Weathermap", 0);

    if (msg && *msg) {
        const char *cls = (strncmp(msg, "\xe2\x9a\xa0", 3) == 0) ? "flash-error" : "flash-ok";
        n += snprintf(buf + n, sz - n, "<p class=\"%s\">%s</p>", cls, msg);
    }

    n += snprintf(
        buf + n, sz - n,
        "<form method=\"POST\" action=\"/weathermap\">"
        "<div class=\"section\">"
        "<h2>Map Center</h2>"
        "<label for=\"lat\">Center latitude (&deg;)<br>"
        "<input id=\"lat\" type=\"text\" inputmode=\"decimal\" "
        "pattern=\"-?[0-9]+(\\.[0-9]+)?\" name=\"text1\" value=\"%s\" required></label>"
        "<label for=\"lon\">Center longitude (&deg;)<br>"
        "<input id=\"lon\" type=\"text\" inputmode=\"decimal\" "
        "pattern=\"-?[0-9]+(\\.[0-9]+)?\" name=\"text2\" value=\"%s\" required></label>"
        "<label for=\"span\">Half width (km)<br>"
        "<input id=\"span\" type=\"text\" inputmode=\"decimal\" "
        "pattern=\"[0-9]+(\\.[0-9]+)?\" name=\"text3\" value=\"%s\" required></label>"
        "<small>Span on ground: %.1f km (width = 2 x half width).<br>"
        "Use '.' as decimal separator.<br>"
        "Saving clears the cached PNG; the device refetches the map on the next update.</small>"
        "</div>"
        "<div style=\"text-align:center\">"
        "<input type=\"submit\" value=\"Save\">"
        "</div>"
        "</form>",
        lat_buf, lon_buf, half_buf, span_km);

    uint32_t staged_bytes = 0;
    bool has_png = get_weathermap_meta(&staged_bytes) && staged_bytes >= 8;
    if (has_png) {
        const uint8_t *data = FLASH_PTR(FIRMWARE_SLOT1_FLASH_OFFSET);
        if (!(data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G'))
            has_png = false;
    }

    uint16_t cached_w = 0, cached_h = 0;
    uint32_t cached_len = 0;
    bool has_2bpp = weathermap_flash_info(&cached_w, &cached_h, &cached_len);

    n += snprintf(buf + n, sz - n, "<div class=\"section\">");
    if (has_png) {
        n += snprintf(buf + n, sz - n,
                      "<p>Cached PNG in slot1: %u bytes.</p>"
                      "<p><img src=\"/weathermap.png\" alt=\"cached map\" "
                      "style=\"max-width:100%%;height:auto;border:1px solid #ccc\"></p>"
                      "<p><a class=\"btn\" href=\"/weathermap_clear\">Clear cached image</a></p>",
                      staged_bytes);
    } else {
        n += snprintf(buf + n, sz - n, "<p><i>No cached map stored in device.</i></p>");
    }

    if (has_2bpp) {
        n += snprintf(buf + n, sz - n,
                      "<p class=\"small\">Rendered 2-bit image: %ux%u, %u bytes.</p>", cached_w,
                      cached_h, cached_len);
    } else {
        n += snprintf(buf + n, sz - n,
                      "<p class=\"small\"><i>No compressed 2-bit image stored.</i></p>");
    }
    n += snprintf(buf + n, sz - n, "</div>");

    n += snprintf(buf + n, sz - n, "<p class=\"small\">%s</p>", timeout_info);
    scaffold_page_close(buf + n, sz - n, "/", NULL);
}

const char *wm_handle_config_form(const char *body, size_t len) {
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

    if (!valid)
        return "\xe2\x9a\xa0 Invalid input \xe2\x80\x93 please use decimal numbers with '.' as "
               "separator.";

    if (lat < -90.0)
        lat = -90.0;
    if (lat > 90.0)
        lat = 90.0;
    if (lon < -180.0)
        lon = -180.0;
    if (lon > 180.0)
        lon = 180.0;
    if (half_km < 1.0 || half_km > 500.0)
        return "\xe2\x9a\xa0 Half width must be between 1 and 500 km.";

    double half_m = half_km * 1000.0;

    weathermap_config_t wcfg;
    if (!load_weathermap_config(&wcfg))
        wcfg = weathermap_config_flash;

    wcfg.data.center_lat = lat;
    wcfg.data.center_lon = lon;
    wcfg.data.half_width_m = half_m;
    wcfg.data.flags = WEATHERMAP_CONFIG_VERSION;

    bool ok = save_weathermap_config(&wcfg);
    if (ok) {
        clear_weathermap_meta();
        dlog("[WEATHERMAP] Config saved: lat=%.6f lon=%.6f half_width=%.1fm\n", lat, lon, half_m);
        return "\xe2\x9c\x94 settings saved. Cached map cleared.";
    } else {
        dlog("[WEATHERMAP] Failed to save config\n");
        return "\xe2\x9a\xa0 Failed to save settings.";
    }
}
