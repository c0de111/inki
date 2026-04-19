#include "web_config.h"
#include "config.h"
#define LOG_MODULE LOG_MOD_WEBSERVER
#include "debug.h"
#include "historian/historian_flash.h"
#include "pico/time.h"
#include "webserver.h"
#include "webserver_scaffold.h"
#include "webserver_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void hist_render_config_page(char *buf, size_t sz, const char *msg) {
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    int n = scaffold_page_open(buf, sz, "Historian Configuration", 30);

    if (msg && *msg) {
        const char *cls = (msg[0] == '\xe2') ? "flash-error" : "flash-ok";
        n += snprintf(buf + n, sz - n, "<p class=\"%s\">%s</p>", cls, msg);
    }

    n += snprintf(
        buf + n, sz - n,
        "<form method=\"POST\" action=\"/historian\">"
        "<div class=\"section\">"
        "<h2>Historian API</h2>"
        "<label>Server IP:<br><input type=\"text\" name=\"text1\" value=\"%d.%d.%d.%d\"></label>"
        "<label>Port:<br><input type=\"number\" name=\"text2\" value=\"%d\"></label>"
        "<label>API Path:<br><input type=\"text\" name=\"text3\" value=\"%s\"></label>"
        "<label>Datapoint ID:<br><input type=\"number\" name=\"text4\" value=\"%d\"></label>"
        "<label>Hours Back:<br><input type=\"number\" name=\"text5\" value=\"%d\"></label>"
        "<label>Display Name:<br><input type=\"text\" name=\"text6\" value=\"%s\"></label>"
        "</div>"
        "<div style=\"text-align:center\">"
        "<input type=\"submit\" value=\"Save\">"
        "</div>"
        "</form>",
        historian_config_flash.data.ip[0], historian_config_flash.data.ip[1],
        historian_config_flash.data.ip[2], historian_config_flash.data.ip[3],
        historian_config_flash.data.port, historian_config_flash.data.path,
        historian_config_flash.data.datapoint_id, historian_config_flash.data.hours_back,
        historian_config_flash.data.display_name);

    n += snprintf(buf + n, sz - n, "<p class=\"small\">%s</p>", timeout_info);
    scaffold_page_close(buf + n, sz - n, "/", NULL);
}

const char *hist_handle_config_form(const char *body, size_t len) {
    webserver_set_shutdown_time(make_timeout_time_ms(USER_INTERACTION_TIMEOUT_MS));

    web_submission_t result = {0};
    parse_form_fields(body, len, &result);

    historian_config_t new_cfg = {.crc32 = 0};

    int ip0, ip1, ip2, ip3;
    if (sscanf(result.text[0], "%d.%d.%d.%d", &ip0, &ip1, &ip2, &ip3) == 4) {
        new_cfg.data.ip[0] = (uint8_t)ip0;
        new_cfg.data.ip[1] = (uint8_t)ip1;
        new_cfg.data.ip[2] = (uint8_t)ip2;
        new_cfg.data.ip[3] = (uint8_t)ip3;
    } else {
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
    if (ok)
        dlog("Historian config saved.\n");
    else
        dlog("Error saving historian config.\n");

    return ok ? "\xe2\x9c\x94 historian settings stored" : "\xe2\x9a\xa0 error saving settings";
}
