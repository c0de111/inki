#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "homematic_client.h"
#include "flash.h"

static void append_prefixed_address(char* out, size_t out_n, const char* address, bool add_prefix) {
    if (add_prefix && strncmp(address, "HmIP-RF.", 8) != 0) {
        snprintf(out, out_n, "HmIP-RF.%s", address);
    } else {
        snprintf(out, out_n, "%s", address);
    }
}

int homematic_build_getvalue(char* buf, size_t n, const char* address, const char* key) {
    return snprintf(buf, n,
        "<?xml version=\"1.0\"?>\n"
        "<methodCall>\n"
        "  <methodName>getValue</methodName>\n"
        "  <params>\n"
        "    <param><value><string>%s</string></value></param>\n"
        "    <param><value><string>%s</string></value></param>\n"
        "  </params>\n"
        "</methodCall>",
        address, key);
}

int homematic_build_multicall(char* buf, size_t n, const homematic_config_t* cfg) {
    if (!buf || !cfg) return -1;
    size_t used = 0;
    int m = snprintf(buf + used, (used < n) ? (n - used) : 0,
                     "<?xml version=\"1.0\"?>\n"
                     "<methodCall>\n"
                     "  <methodName>system.multicall</methodName>\n"
                     "  <params><param><value><array><data>\n");
    if (m < 0) return -1; used += m;

    for (uint8_t i = 0; i < cfg->data.count; i++) {
        char addr[64];
        append_prefixed_address(addr, sizeof(addr), cfg->data.items[i].address, cfg->data.add_interface_prefix);
        m = snprintf(buf + used, (used < n) ? (n - used) : 0,
                     "    <value><struct>\n"
                     "      <member><name>methodName</name><value><string>getValue</string></value></member>\n"
                     "      <member><name>params</name><value><array><data>\n"
                     "        <value><string>%s</string></value>\n"
                     "        <value><string>%s</string></value>\n"
                     "      </data></array></value></member>\n"
                     "    </struct></value>\n",
                     addr, cfg->data.items[i].key);
        if (m < 0) return -1; used += m;
        if (used >= n) return -1;
    }

    m = snprintf(buf + used, (used < n) ? (n - used) : 0,
                 "  </data></array></value></param></params>\n"
                 "</methodCall>");
    if (m < 0) return -1; used += m;
    if (used >= n) return -1;
    return (int)used;
}

int homematic_build_http_post(char* out, size_t n, const char* host, const char* xml, int xml_len) {
    if (!out || !host || !xml || xml_len < 0) return -1;
    return snprintf(out, n,
                    "POST / HTTP/1.0\r\n"
                    "Host: %s\r\n"
                    "Content-Type: text/xml\r\n"
                    "Accept: text/xml\r\n"
                    "User-Agent: inki/0.11\r\n"
                    "Connection: close\r\n"
                    "Content-Length: %d\r\n"
                    "\r\n"
                    "%.*s",
                    host, xml_len, xml_len, xml);
}

int homematic_build_getparamsetdesc(char* buf, size_t n, const char* address) {
    if (!buf || !address) return -1;
    return snprintf(buf, n,
        "<?xml version=\"1.0\"?>\n"
        "<methodCall>\n"
        "  <methodName>getParamsetDescription</methodName>\n"
        "  <params>\n"
        "    <param><value><string>%s</string></value></param>\n"
        "    <param><value><string>VALUES</string></value></param>\n"
        "  </params>\n"
        "</methodCall>",
        address);
}
