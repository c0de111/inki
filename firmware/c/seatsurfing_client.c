#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "seatsurfing_client.h"

#ifdef USE_CASE_SEATSURFING

int seatsurfing_build_http_request(char* buffer, size_t buffer_size,
                                   const char* host,
                                   const char* location_id,
                                   const char* space_id,
                                   const char* auth_b64) {
    if (!buffer || !host || !location_id || !space_id || !auth_b64) {
        return -1;
    }

    int len = snprintf(buffer, buffer_size,
                       "GET /location/%s/space/%s/availability HTTP/1.0\r\n"
                       "Host: %s\r\n"
                       "Authorization: Basic %s\r\n"
                       "\r\n",
                       location_id, space_id, host, auth_b64);

    if (len < 0 || (size_t)len >= buffer_size) {
        debug_log_with_color(COLOR_RED, "[SEATSURFING] HTTP request too large\n");
        return -1;
    }

    return len;
}

#endif // USE_CASE_SEATSURFING

