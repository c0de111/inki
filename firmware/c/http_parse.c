#include "http_parse.h"

#include <stdlib.h>
#include <string.h>

int http_parse_content_length(const char *header) {
    const char *cl = strstr(header, "Content-Length:");
    if (!cl) {
        cl = strstr(header, "content-length:");
    }
    if (!cl)
        return -1;

    cl += 15; // Skip "Content-Length:"
    while (*cl == ' ')
        cl++;

    return atoi(cl);
}

int http_parse_status_code(const char *header) {
    const char *p = strstr(header, "HTTP/");
    if (!p)
        return -1;
    p = strchr(p, ' ');
    if (!p)
        return -1;
    while (*p == ' ')
        p++;
    return atoi(p);
}

void http_parse_host(const char *request, char *out_host, size_t out_size) {
    if (!request || !out_host || out_size == 0)
        return;
    out_host[0] = '\0';
    const char *line = request;
    while (line) {
        if (strncasecmp(line, "Host:", 5) == 0) {
            const char *v = line + 5;
            while (*v == ' ' || *v == '\t')
                v++;
            const char *end = v;
            while (*end && *end != '\r' && *end != '\n')
                end++;
            size_t len = (size_t)(end - v);
            if (len >= out_size)
                len = out_size - 1;
            size_t copy = len;
            for (size_t i = 0; i < len; i++) {
                if (v[i] == ':') {
                    copy = i;
                    break;
                }
            }
            memcpy(out_host, v, copy);
            out_host[copy] = '\0';
            return;
        }
        const char *nl = strchr(line, '\n');
        line = nl ? nl + 1 : NULL;
    }
}
