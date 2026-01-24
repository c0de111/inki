#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "seatsurfing_client.h"
#include <stdbool.h>
#include "third_party/cjson/cJSON.h"

#ifdef USE_CASE_SEATSURFING

int seatsurfing_build_http_request(char* buffer, size_t buffer_size,
                                   const char* host,
                                   const char* location_id,
                                   const char* auth_b64) {
    if (!buffer || !host || !location_id || !auth_b64) {
        return -1;
    }

    int len = snprintf(buffer, buffer_size,
                       "GET /location/%s/space/availability HTTP/1.0\r\n"
                       "Host: %s\r\n"
                       "Authorization: Basic %s\r\n"
                       "\r\n",
                       location_id, host, auth_b64);

    if (len < 0 || (size_t)len >= buffer_size) {
        debug_log_with_color(COLOR_RED, "[SEATSURFING] HTTP request too large\n");
        return -1;
    }

    return len;
}

seat_info_t seatsurfing_parse_seat_info(const char* json, const char* target_space_id_or_name) {
    seat_info_t info = {
        .is_available = true,
        .user_email = {0},
        .desk_name = {0}
    };

    const char* target = (target_space_id_or_name && *target_space_id_or_name) ? target_space_id_or_name : NULL;

    cJSON* root = cJSON_Parse(json);
    if (!root) {
        return info;
    }

    if (cJSON_IsArray(root)) {
        cJSON* item = NULL;
        cJSON_ArrayForEach(item, root) {
            if (!cJSON_IsObject(item)) continue;
            cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "id");
            cJSON* name = cJSON_GetObjectItemCaseSensitive(item, "name");

            bool match = false;
            if (!target) {
                // take the first entry as fallback
                match = true;
            } else {
                if (cJSON_IsString(id) && strcmp(id->valuestring, target) == 0) {
                    match = true;
                } else if (cJSON_IsString(name) && strcmp(name->valuestring, target) == 0) {
                    match = true;
                }
            }

            if (!match) continue;
            if (!target) target = ""; // mark found

            if (cJSON_IsString(name)) {
                strncpy(info.desk_name, name->valuestring, sizeof(info.desk_name) - 1);
            }

            cJSON* available = cJSON_GetObjectItemCaseSensitive(item, "available");
            if (cJSON_IsBool(available)) {
                info.is_available = cJSON_IsTrue(available);
            }

            if (!info.is_available) {
                cJSON* bookings = cJSON_GetObjectItemCaseSensitive(item, "bookings");
                if (cJSON_IsArray(bookings)) {
                    cJSON* b = cJSON_GetArrayItem(bookings, 0);
                    if (cJSON_IsObject(b)) {
                        cJSON* email = cJSON_GetObjectItemCaseSensitive(b, "userEmail");
                        if (cJSON_IsString(email)) {
                            strncpy(info.user_email, email->valuestring, sizeof(info.user_email) - 1);
                        }
                    }
                }
            }
            break; // found target
        }
    }

    cJSON_Delete(root);
    return info;
}
#endif // USE_CASE_SEATSURFING
#include "third_party/cjson/cJSON.h"
