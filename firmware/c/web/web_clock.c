#include "web_clock.h"
#include "config.h"
#define LOG_MODULE LOG_MOD_WEBSERVER
#include "debug.h"
#include "rtc.h"
#include "webserver.h"
#include "webserver_scaffold.h"
#include "webserver_utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void build_clock_page(char *buf, size_t sz, const char *message) {
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    rtc_time_t current;
    rtc_read_time(&current);

    char current_raw[64];
    snprintf(current_raw, sizeof(current_raw), "%02d:%02d:%02d %02d.%02d.%04d", current.hours,
             current.minutes, current.seconds, current.date, current.month, current.year + 2000);

    char current_dst[64];
    rtc_format_time(&current, current_dst, sizeof(current_dst));

    int n = scaffold_page_open(buf, sz, "Set Clock", 0);

    if (message && *message) {
        n += snprintf(buf + n, sz - (size_t)n, "<p class=\"flash-ok\">%s</p>", message);
    }

    n += snprintf(
        buf + n, sz - (size_t)n,
        "<div class=\"section\">"
        "<p>RTC (raw): <span class=\"value\">%s</span></p>"
        "<p>RTC (DST): <span class=\"value\">%s</span></p>"
        "</div>"
        "<form id=\"clockForm\" method=\"POST\" action=\"/clock\">"
        "<div class=\"section\" style=\"text-align:center\">"
        "<input type=\"hidden\" name=\"line\" id=\"line\">"
        "<p id=\"preview\">Determining local time...</p>"
        "<input type=\"submit\" value=\"Set Clock\">"
        "</div>"
        "</form>"
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
        "</script>",
        current_raw, current_dst);

    n += snprintf(buf + n, sz - (size_t)n, "<p class=\"small\">%s</p>", timeout_info);
    if ((size_t)n >= sz)
        n = (int)sz - 1;
    scaffold_page_close(buf + n, sz - (size_t)n, "/", NULL);
}

void handle_form_clock(const char *body, size_t len, char *buf, size_t sz) {
    (void)len;
    const char *line_param = strstr(body, "line=");
    if (!line_param) {
        dlog("POST /clock: Kein line= Parameter\n");
        build_clock_page(buf, sz, "\xe2\x9d\x8c Kein line= Parameter.");
        return;
    }

    char raw_line[128] = {0};
    char decoded_line[128] = {0};
    sscanf(line_param + 5, "%127[^&\r\n]", raw_line);
    url_decode(decoded_line, raw_line, sizeof(decoded_line));

    dlog("POST /clock: line = ");
    dlog(decoded_line);
    dlog("\n");

    rtc_time_t old_time, new_time;

    rtc_read_time(&old_time);
    rtc_set_time_from_string(decoded_line);
    rtc_read_time(&new_time);

    int old_min = old_time.hours * 60 + old_time.minutes;
    int new_min = new_time.hours * 60 + new_time.minutes;
    int delta = new_min - old_min;

    char msg[256];
    snprintf(msg, sizeof(msg),
             "\xe2\x9c\x94\xef\xb8\x8f Uhrzeit gesetzt<br>"
             "Vorher: %02d:%02d&nbsp;am&nbsp;%02d.%02d.%04d<br>"
             "Jetzt: %02d:%02d&nbsp;am&nbsp;%02d.%02d.%04d<br>"
             "Differenz: <b>%d&nbsp;Minute%s</b>",
             old_time.hours, old_time.minutes, old_time.date, old_time.month, old_time.year + 2000,
             new_time.hours, new_time.minutes, new_time.date, new_time.month, new_time.year + 2000,
             abs(delta), abs(delta) == 1 ? "" : "n");

    build_clock_page(buf, sz, msg);
}
