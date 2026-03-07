#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "debug.h"
#include "historian_client.h"
#include "historian_config.h"
#include "http_client.h" // for HTTP_JSON_BODY_MAX

#ifdef USE_CASE_HISTORIAN

// Time conversions
static uint64_t historian_rtc_to_unix_ms(const ds3231_data_t *rtc_time) {
    struct tm timeinfo = {0};
    timeinfo.tm_year = rtc_time->year + 100; // tm_year is years since 1900
    timeinfo.tm_mon = rtc_time->month - 1;   // tm_mon is 0-11
    timeinfo.tm_mday = rtc_time->date;
    timeinfo.tm_hour = rtc_time->hours;
    timeinfo.tm_min = rtc_time->minutes;
    timeinfo.tm_sec = rtc_time->seconds;
    timeinfo.tm_isdst = 0; // RTC has no DST info

    setenv("TZ", "CET-1", 1); // MEZ = UTC+1
    tzset();

    time_t unix_seconds = mktime(&timeinfo); // interprets as MEZ, returns UTC
    return (uint64_t)unix_seconds * 1000ULL;
}

uint64_t historian_get_current_unix_ms(ds3231_t *clock) {
    ds3231_data_t current_time;
    ds3231_read_current_time(clock, &current_time);
    return historian_rtc_to_unix_ms(&current_time);
}

uint64_t historian_get_unix_ms_hours_ago(ds3231_t *clock, int hours) {
    uint64_t now_ms = historian_get_current_unix_ms(clock);
    uint64_t hours_in_ms = (uint64_t)hours * 3600ULL * 1000ULL;
    return now_ms - hours_in_ms;
}

void historian_unix_ms_to_local_string(uint64_t unix_ms, char *buffer, size_t size) {
    time_t unix_seconds = unix_ms / 1000;
    const struct tm *timeinfo = gmtime(&unix_seconds);

    bool is_dst = false;
    if (timeinfo->tm_mon >= 2 && timeinfo->tm_mon <= 9) { // March(2) to October(9)
        is_dst = true;
    }

    time_t local_seconds = unix_seconds + 3600; // +1h for MEZ
    if (is_dst) {
        local_seconds += 3600; // +1h for MESZ
    }

    const struct tm *local_time = gmtime(&local_seconds);

    snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d %s", local_time->tm_year + 1900,
             local_time->tm_mon + 1, local_time->tm_mday, local_time->tm_hour, local_time->tm_min,
             local_time->tm_sec, is_dst ? "MESZ" : "MEZ");
}

// HTTP request build
int historian_build_http_request(char *buffer, size_t buffer_size, const char *host,
                                 int datapoint_id, uint64_t start_time_ms, uint64_t end_time_ms) {
    static char json_body[HTTP_JSON_BODY_MAX];
    int json_len =
        snprintf(json_body, sizeof(json_body),
                 "{"
                 "\"id\":%d,"
                 "\"method\":\"getTimeSeries\","
                 "\"params\":[%d,%llu,%llu]"
                 "}",
                 123, // Request-ID (could be dynamic)
                 datapoint_id, (unsigned long long)start_time_ms, (unsigned long long)end_time_ms);

    if (json_len >= (int)sizeof(json_body)) {
        debug_log_with_color(COLOR_RED, "[HISTORIAN] JSON body too large\n");
        return -1;
    }

    int request_len = snprintf(buffer, buffer_size,
                               "POST /query/jsonrpc.gy HTTP/1.0\r\n"
                               "Host: %s\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: %d\r\n"
                               "\r\n"
                               "%s",
                               host, json_len, json_body);

    if (request_len >= (int)buffer_size) {
        debug_log_with_color(COLOR_RED, "[HISTORIAN] HTTP request too large\n");
        return -1;
    }

    return request_len;
}

// JSON parsing and display prep
bool historian_parse_timeseries(const char *json, TimeSeries *result) {
    if (!json || !result)
        return false;

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        debug_log("[HISTORIAN] JSON parse failed\n");
        return false;
    }

    cJSON *res = cJSON_GetObjectItem(root, "result");
    if (!res) {
        debug_log("[HISTORIAN] No 'result' in JSON\n");
        cJSON_Delete(root);
        return false;
    }

    cJSON *dataPoint = cJSON_GetObjectItem(res, "dataPoint");
    if (dataPoint) {
        cJSON *dp_id = cJSON_GetObjectItem(dataPoint, "id");
        if (dp_id) {
            cJSON *interfaceId = cJSON_GetObjectItem(dp_id, "interfaceId");
            if (interfaceId && cJSON_IsString(interfaceId)) {
                strncpy(result->name, interfaceId->valuestring, sizeof(result->name) - 1);
            }
        }

        cJSON *attributes = cJSON_GetObjectItem(dataPoint, "attributes");
        if (attributes) {
            cJSON *unit = cJSON_GetObjectItem(attributes, "unit");
            if (unit && cJSON_IsString(unit)) {
                strncpy(result->unit, unit->valuestring, sizeof(result->unit) - 1);
            }
        }
    }

    cJSON *timestamps = cJSON_GetObjectItem(res, "timestamps");
    cJSON *values = cJSON_GetObjectItem(res, "values");
    cJSON *states = cJSON_GetObjectItem(res, "states");

    if (!timestamps || !values) {
        debug_log("[HISTORIAN] Missing timestamps or values\n");
        cJSON_Delete(root);
        return false;
    }

    int count = cJSON_GetArraySize(timestamps);
    if (count > MAX_DATA_POINTS)
        count = MAX_DATA_POINTS;

    for (int i = 0; i < count; i++) {
        cJSON *ts = cJSON_GetArrayItem(timestamps, i);
        cJSON *val = cJSON_GetArrayItem(values, i);

        if (ts && val) {
            result->points[i].timestamp = (uint64_t)cJSON_GetNumberValue(ts);
            result->points[i].value = (float)cJSON_GetNumberValue(val);

            if (states) {
                cJSON *state = cJSON_GetArrayItem(states, i);
                if (state) {
                    result->points[i].state = (uint8_t)cJSON_GetNumberValue(state);
                } else {
                    result->points[i].state = 0;
                }
            } else {
                result->points[i].state = 0;
            }
        }
    }

    result->count = count;

    if (count > 0) {
        result->min_value = result->max_value = result->points[0].value;
        for (int i = 1; i < count; i++) {
            if (result->points[i].value < result->min_value)
                result->min_value = result->points[i].value;
            if (result->points[i].value > result->max_value)
                result->max_value = result->points[i].value;
        }
        result->last_value = result->points[count - 1].value;
    }

    cJSON_Delete(root);

    debug_log("[HISTORIAN] Parsed %d data points (min=%.2f, max=%.2f)\n", count, result->min_value,
              result->max_value);

    return true;
}

void historian_prepare_display_data(TimeSeries *series, int target_points) {
    if (!series || series->count <= target_points) {
        return;
    }

    debug_log("historian_prepare_display_data: downsampling %d points to %d\n", series->count,
              target_points);

    int step = series->count / target_points;
    if (step < 2)
        step = 2;

    int new_count = 0;
    for (int i = 0; i < series->count && new_count < target_points; i += step) {
        if (new_count < i) {
            series->points[new_count] = series->points[i];
        }
        new_count++;
    }

    if (new_count < target_points && series->count > 0) {
        series->points[new_count] = series->points[series->count - 1];
        new_count++;
    }

    series->count = new_count;
    debug_log("historian_prepare_display_data: result has %d points\n", new_count);
}

#endif // USE_CASE_HISTORIAN
