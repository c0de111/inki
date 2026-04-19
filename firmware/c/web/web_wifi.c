#include "web_wifi.h"
#include "config.h"
#define LOG_MODULE LOG_MOD_WEBSERVER
#include "debug.h"
#include "flash.h"
#include "webserver.h"
#include "webserver_scaffold.h"
#include "webserver_utils.h"
#include <stdio.h>
#include <string.h>

void build_wifi_config_page(char *buf, size_t sz, const char *message) {
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    int n = scaffold_page_open(buf, sz, "Wi-Fi Settings", 0);

    if (message && *message) {
        n += snprintf(buf + n, sz - (size_t)n, "<p class=\"flash-ok\">%s</p>", message);
    }

    n += snprintf(buf + n, sz - (size_t)n,
                  "<form method=\"POST\" action=\"/wifi\">"
                  "<div class=\"section\">"
                  "<h2>Wi-Fi Credentials</h2>"
                  "<label>SSID:<br><input type=\"text\" name=\"text1\" value=\"%s\"></label>"
                  "<label>Password:<br><input type=\"text\" name=\"text2\" value=\"%s\"></label>"
                  "</div>"
                  "<div class=\"section\" style=\"text-align:center\">"
                  "<input type=\"submit\" value=\"Save\">"
                  "</div>"
                  "</form>",
                  wifi_config_flash.ssid, wifi_config_flash.password);

    n += snprintf(buf + n, sz - (size_t)n, "<p class=\"small\">%s</p>", timeout_info);
    if ((size_t)n >= sz)
        n = (int)sz - 1;
    scaffold_page_close(buf + n, sz - (size_t)n, "/", NULL);
}

void handle_form_wifi(const char *body, size_t len, char *buf, size_t sz) {
    web_submission_t result = {0};

    parse_form_fields(body, len, &result);

    wifi_config_t new_cfg = {.crc32 = 0};
    strncpy(new_cfg.ssid, result.text[0], sizeof(new_cfg.ssid) - 1);
    strncpy(new_cfg.password, result.text[1], sizeof(new_cfg.password) - 1);

    bool ok = save_wifi_config(&new_cfg);

    build_wifi_config_page(buf, sz, "\xe2\x9c\x94 WiFi data saved");

    if (ok) {
        dlog("SSID & password saved\n");
    } else {
        dlog("Error saving data\n");
    }
}
