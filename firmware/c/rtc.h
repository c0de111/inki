#ifndef RTC_H
#define RTC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct rtc_time_t {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day; // weekday: 1=Monday .. 7=Sunday
    uint8_t date;
    uint8_t month;
    uint8_t year; // offset from 2000
} rtc_time_t;

void rtc_init(void);
bool rtc_is_available(void);
int rtc_read_time(rtc_time_t *out);
void rtc_set_time_from_string(const char *line);
void rtc_sync_from_server(void);
void rtc_set_alarm(int page);
void rtc_disable_alarm(void);
bool rtc_alarm_flag_is_set(void);
bool rtc_supports_temperature(void);
float rtc_read_temperature_c(void);
bool rtc_is_dst_europe(const rtc_time_t *t);
void rtc_format_time(const rtc_time_t *t, char *buffer, size_t buffer_size);
void rtc_format_time_short(const rtc_time_t *t, char *buffer, size_t buffer_size);
// Format as ISO 8601 UTC: "2026-04-08T09:30:00Z" (buffer must be >= 21 bytes)
void rtc_to_iso8601(const rtc_time_t *t, char *buffer, size_t buffer_size);
const char *rtc_day_name(int day);
const char *rtc_month_name(int month);
// Parse HTTP Date header (RFC 7231) into rtc_time_t (UTC). Returns false if header
// absent/malformed.
bool rtc_parse_http_date(const char *header, rtc_time_t *out);

#endif
