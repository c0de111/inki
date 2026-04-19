#include "web_config.h"
#include "config.h"
#define LOG_MODULE LOG_MOD_WEBSERVER
#include "debug.h"
#include "homematic/homematic_flash.h"
#include "pico/time.h"
#include "webserver.h"
#include "webserver_scaffold.h"
#include "webserver_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void hm_render_config_page(char *buf, size_t sz, const char *msg) {
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    char ip_string[16];
    snprintf(ip_string, sizeof(ip_string), "%d.%d.%d.%d", homematic_config_flash.data.ip[0],
             homematic_config_flash.data.ip[1], homematic_config_flash.data.ip[2],
             homematic_config_flash.data.ip[3]);

    int n = scaffold_page_open(buf, sz, "Homematic Configuration", 30);

    if (msg && *msg) {
        const char *cls = (msg[0] == '\xe2') ? "flash-error" : "flash-ok";
        n += snprintf(buf + n, sz - n, "<p class=\"%s\">%s</p>", cls, msg);
    }

    n += snprintf(buf + n, sz - n,
                  "<form method=\"POST\" action=\"/homematic\">"
                  "<div class=\"section\">"
                  "<h2>Homematic API</h2>"
                  "<label>Server IP:<br><input type=\"text\" name=\"text1\" value=\"%s\"></label>"
                  "<label>Port:<br><input type=\"number\" name=\"text2\" value=\"%d\"></label>"
                  "</div>",
                  ip_string, homematic_config_flash.data.port);

    n += snprintf(buf + n, sz - n,
                  "<div class=\"section\">"
                  "<h2>Data Points</h2>"
                  "<div class=\"diag-wrap\">"
                  "<table class=\"diag-table\">"
                  "<colgroup>"
                  "<col style=\"width:6%%\">"
                  "<col style=\"width:30%%\">"
                  "<col style=\"width:38%%\">"
                  "<col style=\"width:26%%\">"
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
        n += snprintf(buf + n, sz - n,
                      "<tr><td>%d</td>"
                      "<td><input type=\"text\" name=\"text%d\" value=\"%s\"></td>"
                      "<td><input type=\"text\" name=\"text%d\" value=\"%s\"></td>"
                      "<td><input type=\"text\" name=\"text%d\" value=\"%s\"></td></tr>",
                      i + 1, 5 + 3 * i, addr, 6 + 3 * i, key, 7 + 3 * i, lab);
    }

    n += snprintf(buf + n, sz - n,
                  "</table>"
                  "</div>"
                  "</div>"
                  "<div style=\"text-align:center\">"
                  "<input type=\"submit\" value=\"Save\">"
                  "</div>"
                  "</form>");

    n += snprintf(buf + n, sz - n, "<p class=\"small\">%s</p>", timeout_info);
    scaffold_page_close(buf + n, sz - n, "/", NULL);
}

const char *hm_handle_config_form(const char *body, size_t len) {
    webserver_set_shutdown_time(make_timeout_time_ms(USER_INTERACTION_TIMEOUT_MS));

    web_submission_t result = {0};
    parse_form_fields(body, len, &result);

    homematic_config_t new_cfg = {.crc32 = 0};

    int ip0, ip1, ip2, ip3;
    if (sscanf(result.text[0], "%d.%d.%d.%d", &ip0, &ip1, &ip2, &ip3) == 4) {
        new_cfg.data.ip[0] = (uint8_t)ip0;
        new_cfg.data.ip[1] = (uint8_t)ip1;
        new_cfg.data.ip[2] = (uint8_t)ip2;
        new_cfg.data.ip[3] = (uint8_t)ip3;
    } else {
        memcpy(new_cfg.data.ip, homematic_config_flash.data.ip, 4);
    }

    int port = atoi(result.text[1]);
    new_cfg.data.port =
        (port > 0 && port < 65536) ? (uint16_t)port : homematic_config_flash.data.port;

    new_cfg.data.add_interface_prefix = homematic_config_flash.data.add_interface_prefix;
    new_cfg.data.auto_label = homematic_config_flash.data.auto_label;

    uint8_t count = 0;
    for (int i = 0; i < HOMEMATIC_MAX_ITEMS; i++) {
        const char *addr = result.text[4 + 3 * i];
        const char *key = result.text[5 + 3 * i];
        const char *lab = result.text[6 + 3 * i];

        if (addr[0] == '\0' && key[0] == '\0' && lab[0] == '\0')
            continue;
        strncpy(new_cfg.data.items[count].address, addr,
                sizeof(new_cfg.data.items[count].address) - 1);
        strncpy(new_cfg.data.items[count].key, key, sizeof(new_cfg.data.items[count].key) - 1);
        strncpy(new_cfg.data.items[count].fallback_label, lab,
                sizeof(new_cfg.data.items[count].fallback_label) - 1);
        count++;
    }
    new_cfg.data.count = count;

    bool ok = save_homematic_config(&new_cfg);
    if (ok)
        dlog("Homematic config saved.\n");
    else
        dlog("Error saving homematic config.\n");

    return ok ? "\xe2\x9c\x94 homematic settings stored" : "\xe2\x9a\xa0 error saving settings";
}
