#include "webserver_scaffold.h"
#include <stdio.h>
#include <string.h>

// Unified CSS: device-status card layout + form styling + responsive breakpoints.
// This is a plain C string (not a format string) — literal '%' is safe.
static const char s_css[] =
    ":root{color-scheme:light}"
    "*{box-sizing:border-box}"
    "body{margin:0;padding:.8rem;font-family:-apple-system,BlinkMacSystemFont,"
    "\"Segoe UI\",Roboto,Helvetica,Arial,sans-serif;background:#f5f7fa;"
    "color:#1f2937;line-height:1.45;text-align:center}"
    ".container{max-width:800px;margin:0 auto}"
    "h1{margin:.25rem 0 .85rem;font-size:1.4rem;text-align:center}"
    "h2{margin:0 0 .45rem;font-size:1.02rem;text-align:center}"
    ".value{font-weight:700}"
    ".mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,"
    "\"Liberation Mono\",monospace}"
    ".green{color:#0f7b0f}"
    ".orange{color:#b56900}"
    ".red{color:#b42318}"
    ".section{background:#fff;border:1px solid #d7dde5;border-radius:10px;"
    "margin:0 auto .72rem;padding:.72rem .82rem;max-width:540px;"
    "box-shadow:0 1px 2px rgba(16,24,40,.04)}"
    ".kv{width:100%;max-width:640px;margin:0 auto;border-collapse:collapse}"
    ".kv td{padding:.2rem .25rem;vertical-align:top;text-align:center}"
    ".kv td:first-child{width:42%;color:#5b6675}"
    ".diag-wrap{width:100%;overflow-x:auto;-webkit-overflow-scrolling:touch}"
    ".diag-table{width:100%;min-width:460px;margin:0 auto;border-collapse:collapse;"
    "font-size:.94rem}"
    ".diag-table th,.diag-table td{border-top:1px solid #e6ebf1;"
    "padding:.35rem .25rem;text-align:center;vertical-align:top}"
    ".diag-table th{color:#4b5563;font-weight:700;border-top:none}"
    ".small{font-size:.88rem;color:#5b6675}"
    ".section pre{margin:.4rem auto .25rem;padding:.45rem .5rem;overflow-x:auto;"
    "background:#f8fafc;border-radius:6px;border:1px solid #e5e7eb;"
    "text-align:left;max-width:640px}"
    /* Navigation links */
    "a{display:block;margin:.9rem 0 .25rem;text-align:center;"
    "text-decoration:none;color:#0b5ed7;font-weight:600}"
    "a.btn{display:inline-block;padding:.6em 1em;font-size:1em;margin:.5em;"
    "width:80%;max-width:200px;background:#eee;border:1px solid #ccc;"
    "border-radius:5px;color:#000;font-weight:normal}"
    "a.btn:hover{background:#ddd}"
    /* Form elements */
    "form{margin:0 auto;text-align:left}"
    "label{display:block;margin-top:.6em;font-weight:bold}"
    "label.inline{display:inline-block;margin-right:.8em;font-weight:normal}"
    "input[type=text],input[type=number],input[type=password],textarea,select{"
    "width:100%;padding:.45em .5em;font-size:1em;border:1px solid #ccc;"
    "border-radius:4px;box-sizing:border-box;margin-top:.2em}"
    "input[type=radio],input[type=checkbox]{width:auto;margin-top:0}"
    "input[type=submit],button[type=submit]{margin-top:1em;padding:.6em 1.2em;"
    "font-size:1em;border:1px solid #aaa;border-radius:6px;background:#fff;"
    "cursor:pointer}"
    "input[type=submit]:hover,button[type=submit]:hover{background:#f0f0f0}"
    "input[type=file]{font-size:1em;padding:.5em;margin:.5em auto;display:block;"
    "width:80%;max-width:300px}"
    "fieldset{border:1px solid #d7dde5;border-radius:8px;padding:.8em 1em;"
    "margin-top:1em}"
    "legend{font-weight:bold;color:#374151}"
    "fieldset[disabled]{opacity:.45}"
    "progress{width:80%;max-width:300px;height:2em;margin-top:1em}"
    ".flash-ok{color:#0f7b0f;font-weight:600}"
    ".flash-error{color:#b42318;font-weight:600}"
    /* Responsive */
    "@media(min-width:700px){body{padding:1rem}h1{font-size:1.58rem}"
    ".kv td:first-child{width:34%}}"
    "@media(max-width:560px){.kv{max-width:100%}"
    ".kv td,.kv td:first-child{display:block;width:100%;text-align:center}"
    ".kv tr{display:block;padding:.22rem 0;border-top:1px solid #edf1f6}"
    ".kv tr:first-child{border-top:none}"
    ".diag-table{min-width:430px;font-size:.89rem}"
    ".section{max-width:100%}}";

int scaffold_page_open(char *buf, size_t sz, const char *title, int auto_refresh_s) {
    int n = 0;

    int w = snprintf(buf, sz,
                     "<!DOCTYPE html><html><head>"
                     "<meta charset=\"UTF-8\">"
                     "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                     "<title>%s</title>",
                     title);
    if (w > 0 && (size_t)w < sz)
        n = w;
    else
        return n;

    if (auto_refresh_s > 0) {
        w = snprintf(buf + n, sz - (size_t)n, "<meta http-equiv=\"refresh\" content=\"%d\">",
                     auto_refresh_s);
        if (w > 0)
            n += w;
    }

    // Append CSS as a raw string (not a format string — no %% escaping needed)
    size_t css_len = sizeof(s_css) - 1;
    size_t avail = (size_t)n < sz - 1 ? sz - 1 - (size_t)n : 0;
    if (avail > 7) { // room for <style>...</style>
        memcpy(buf + n, "<style>", 7);
        n += 7;
        avail -= 7;
        size_t copy = css_len < avail ? css_len : avail;
        memcpy(buf + n, s_css, copy);
        n += (int)copy;
        buf[n] = '\0';
    }

    w = snprintf(buf + n, sz - (size_t)n,
                 "</style></head><body><div class=\"container\"><h1>%s</h1>", title);
    if (w > 0)
        n += w;

    return n;
}

int scaffold_page_close(char *buf, size_t sz, const char *back_href, const char *footer_html) {
    int n = 0;

    if (footer_html && *footer_html) {
        int w = snprintf(buf, sz, "<div class=\"section small\">%s</div>", footer_html);
        if (w > 0)
            n += w;
    }

    if (back_href && *back_href) {
        int w = snprintf(buf + n, sz > (size_t)n ? sz - (size_t)n : 0, "<a href=\"%s\">Back</a>",
                         back_href);
        if (w > 0)
            n += w;
    }

    int w = snprintf(buf + n, sz > (size_t)n ? sz - (size_t)n : 0, "</div></body></html>");
    if (w > 0)
        n += w;

    return n;
}
