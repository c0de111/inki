#include "rtc.h"
#include "config.h"
#include "debug.h"
#include "ds3231.h"
#include "flash.h"
#include "http_client.h"
#include "i2c_bus.h"
#include "rv3028.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    RTC_BACKEND_NONE = 0,
    RTC_BACKEND_DS3231,
    RTC_BACKEND_RV3028,
} rtc_backend_t;

static ds3231_t ds3231_ctx;
static rv3028_t rv3028_ctx;
static rtc_backend_t active_rtc_backend = RTC_BACKEND_NONE;

#define RTC_TIMEZONE_OFFSET_H 1
#define RTC_AUTOSET_THRESHOLD_S 60

// Apply +1h (to_local=true) or -1h (to_local=false) DST offset, wrapping 0-23.
static int dst_adjust_hour(int hour, bool to_local) {
    hour += to_local ? 1 : -1;
    if (hour >= 24)
        hour -= 24;
    if (hour < 0)
        hour += 24;
    return hour;
}

const char *rtc_day_name(int day) {
    switch (day) {
    case 1:
        return "Monday";
    case 2:
        return "Tuesday";
    case 3:
        return "Wednesday";
    case 4:
        return "Thursday";
    case 5:
        return "Friday";
    case 6:
        return "Saturday";
    case 7:
        return "Sunday";
    default:
        return "Invalid";
    }
}

const char *rtc_month_name(int month) {
    switch (month) {
    case 1:
        return "January";
    case 2:
        return "February";
    case 3:
        return "March";
    case 4:
        return "April";
    case 5:
        return "May";
    case 6:
        return "June";
    case 7:
        return "July";
    case 8:
        return "August";
    case 9:
        return "September";
    case 10:
        return "October";
    case 11:
        return "November";
    case 12:
        return "December";
    default:
        return "Invalid";
    }
}

bool rtc_is_dst_europe(const rtc_time_t *t) {
    int year = 2000 + t->year;
    int month = t->month;
    int day = t->date;
    int hour = t->hours;
    int minute = t->minutes;

    if (month < 3 || month > 10)
        return false;
    if (month > 3 && month < 10)
        return true;

    // Last Sunday of March or October
    int last_sunday = 31 - ((5 * year / 4 + (month == 3 ? 4 : 1)) % 7);

    if (month == 3) {
        return (day > last_sunday) ||
               (day == last_sunday && (hour > 1 || (hour == 1 && minute >= 0)));
    } else { // October
        return (day < last_sunday) ||
               (day == last_sunday && (hour < 2 || (hour == 2 && minute == 0)));
    }
}

void rtc_format_time(const rtc_time_t *t, char *buffer, size_t buffer_size) {
    int display_hour = t->hours;
    if (rtc_is_dst_europe(t))
        display_hour = dst_adjust_hour(display_hour, true);

    snprintf(buffer, buffer_size, "%02i:%02i, %s, %02i. %s %04i", display_hour, t->minutes,
             rtc_day_name(t->day), t->date, rtc_month_name(t->month), 2000 + t->year);
}

void rtc_format_time_short(const rtc_time_t *t, char *buffer, size_t buffer_size) {
    int hour = t->hours;
    if (rtc_is_dst_europe(t))
        hour = dst_adjust_hour(hour, true);
    snprintf(buffer, buffer_size, "%02i:%02i", hour, t->minutes);
}

void rtc_init(void) {
    i2c_probe_result_t i2c_probe = {0};
    i2c_probe_expected_devices(&i2c_probe);

    const bool ds3231_present = i2c_probe.ds3231_present;
    const bool rv3028_present = i2c_probe.rv3028_present;

    if (ds3231_present && rv3028_present) {
        debug_log_with_color(COLOR_YELLOW, "Both DS3231 and RV-3028 detected, using DS3231\n");
        ds3231_init(&ds3231_ctx, i2c_default, DS3231_DEVICE_ADRESS, AT24C32_EEPROM_ADRESS_0);
        active_rtc_backend = RTC_BACKEND_DS3231;
        return;
    }

    if (ds3231_present) {
        ds3231_init(&ds3231_ctx, i2c_default, DS3231_DEVICE_ADRESS, AT24C32_EEPROM_ADRESS_0);
        active_rtc_backend = RTC_BACKEND_DS3231;
        debug_status("OK", "RTC: DS3231\n");
        return;
    }

    if (rv3028_present) {
        if (rv3028_init(&rv3028_ctx, i2c_default, RV3028_ADDR_DEFAULT) == 0) {
            active_rtc_backend = RTC_BACKEND_RV3028;
            debug_status("OK", "RTC: RV-3028\n");
            return;
        }
        debug_status("ERROR", "RV-3028 detected but init failed\n");
    }

    active_rtc_backend = RTC_BACKEND_NONE;
    debug_status("ERROR", "No supported RTC detected\n");
}

bool rtc_supports_temperature(void) { return (active_rtc_backend == RTC_BACKEND_DS3231); }

float rtc_read_temperature_c(void) {
    if (!rtc_supports_temperature())
        return NAN;

    float t = 0.0f;
    if (ds3231_read_temperature(&ds3231_ctx, &t) != 0) {
        debug_log_with_color(COLOR_RED, "DS3231 temperature read failed\n");
        return NAN;
    }
    debug_log("DS3231 temperature: %.2f °C\n", t);
    return t;
}

bool rtc_alarm_flag_is_set(void) {
    bool flag = false;
    if (active_rtc_backend == RTC_BACKEND_DS3231) {
        if (ds3231_read_alarm2_flag(&ds3231_ctx, &flag) != 0)
            return false;
    } else if (active_rtc_backend == RTC_BACKEND_RV3028) {
        if (rv3028_read_alarm_flag(&rv3028_ctx, &flag) != 0)
            return false;
    }
    return flag;
}

// cppcheck-suppress unusedFunction
bool rtc_is_available(void) { return (active_rtc_backend != RTC_BACKEND_NONE); }

int rtc_read_time(rtc_time_t *out) {
    if (!out)
        return -1;
    if (active_rtc_backend == RTC_BACKEND_NONE)
        return -1;

    if (active_rtc_backend == RTC_BACKEND_RV3028) {
        rv3028_time_t rv_time = {0};
        const int rc = rv3028_read_time(&rv3028_ctx, &rv_time);
        if (rc != 0)
            return rc;

        *out = (rtc_time_t){
            .seconds = rv_time.seconds,
            .minutes = rv_time.minutes,
            .hours = rv_time.hours,
            .day = rv_time.day,
            .date = rv_time.date,
            .month = rv_time.month,
            .year = rv_time.year,
        };
        return 0;
    }

    ds3231_data_t ds_data = {0};
    const int rc = ds3231_read_current_time(&ds3231_ctx, &ds_data);
    if (rc != 0)
        return rc;

    *out = (rtc_time_t){
        .seconds = ds_data.seconds,
        .minutes = ds_data.minutes,
        .hours = ds_data.hours,
        .day = ds_data.day,
        .date = ds_data.date,
        .month = ds_data.month,
        .year = ds_data.year,
    };
    return 0;
}

static int rtc_set_time(const rtc_time_t *t) {
    if (active_rtc_backend == RTC_BACKEND_NONE)
        return -1;

    if (active_rtc_backend == RTC_BACKEND_RV3028) {
        rv3028_time_t rv = {
            .seconds = t->seconds,
            .minutes = t->minutes,
            .hours = t->hours,
            .day = t->day,
            .date = t->date,
            .month = t->month,
            .year = t->year,
        };
        return rv3028_set_time(&rv3028_ctx, &rv);
    }

    ds3231_data_t ds = {
        .seconds = t->seconds,
        .minutes = t->minutes,
        .hours = t->hours,
        .day = t->day,
        .date = t->date,
        .month = t->month,
        .year = t->year,
        .century = 1,
        .am_pm = false,
    };
    return ds3231_configure_time(&ds3231_ctx, &ds);
}

static int month_from_short_name(const char *name) {
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; i++) {
        if (strncmp(name, months[i], 3) == 0)
            return i + 1;
    }
    return 0;
}

static int weekday_from_name(const char *name) {
    const char *days[] = {"Monday", "Tuesday",  "Wednesday", "Thursday",
                          "Friday", "Saturday", "Sunday"};
    for (int i = 0; i < 7; i++) {
        if (strncmp(name, days[i], strlen(days[i])) == 0)
            return i + 1;
    }
    return 0;
}

void rtc_set_time_from_string(const char *line) {
    if (active_rtc_backend == RTC_BACKEND_NONE) {
        debug_log_with_color(COLOR_YELLOW, "RTC set skipped: no RTC backend\n");
        return;
    }

    char weekday_str[10] = {0};
    char month_str[4] = {0};
    int day, month, year, hour, minute;

    debug_log("RTC set requested from line: ");
    debug_log(line);

    // Format: "Sunday, 21. Apr 2025, 13:45"
    int parsed = sscanf(line, "%9[^,], %d. %3s %d , %d:%d", weekday_str, &day, month_str, &year,
                        &hour, &minute);

    if (parsed != 6) {
        debug_log("RTC time parse failed.\n");
        debug_log("sscanf parsed items: ");
        char buf[2];
        snprintf(buf, sizeof(buf), "%d", parsed);
        debug_log(buf);
        debug_log("\n");
        return;
    }

    int weekday = weekday_from_name(weekday_str);
    if (weekday == 0) {
        debug_log("Invalid weekday name.\n");
        return;
    }

    month = month_from_short_name(month_str);
    if (month == 0) {
        debug_log("Invalid month name in RTC string.\n");
        return;
    }

    rtc_time_t temp = {
        .year = year - 2000, .month = month, .date = day, .hours = hour, .minutes = minute};

    if (rtc_is_dst_europe(&temp)) {
        hour = dst_adjust_hour(hour, false);
        if (hour == 23) { // wrapped backwards past midnight
            day -= 1;
            if (day == 0) {
                debug_log("DST adjustment underflowed date — skipping RTC set.\n");
                return;
            }
        }
    }

    debug_log("Final time to set: ");
    char msg[64];
    snprintf(msg, sizeof(msg), "%02d:%02d %02d.%02d.%04d (weekday: %d)", hour, minute, day, month,
             year, weekday);
    debug_log(msg);

    rtc_time_t new_time = {.seconds = 0,
                           .minutes = minute,
                           .hours = hour,
                           .day = weekday,
                           .date = day,
                           .month = month,
                           .year = year - 2000};

    rtc_set_time(&new_time);
}

void rtc_set_alarm(int page) {
    if (active_rtc_backend == RTC_BACKEND_NONE) {
        debug_status("WARN", "No RTC: skipping alarm\n");
        return;
    }

    rtc_time_t current_time;
    rtc_read_time(&current_time);

    int local_hour = current_time.hours;
    int local_minute = current_time.minutes;
    int day = current_time.day;

    bool dst_active = rtc_is_dst_europe(&current_time);
    if (dst_active) {
        local_hour = dst_adjust_hour(local_hour, true);
        if (local_hour < current_time.hours) // wrapped past midnight
            day = (day % 7) + 1;
    }

    int refresh = device_config_flash.data.refresh_minutes_by_pushbutton[page & 0x07];
    int total_minutes = local_hour * 60 + local_minute + refresh;
    int alarm_hour = (total_minutes / 60) % 24;
    int alarm_minute = total_minutes % 60;

    if (device_config_flash.data.query_only_at_officehours) {
        if (day == 6 || day == 7) {
            debug_log("Skipping operation: Weekend detected.\n");
            alarm_hour = 6;
            alarm_minute = 0;
        }
        if (alarm_hour >= 19 || alarm_hour < 6) {
            alarm_hour = 6;
            alarm_minute = 0;
        }
    }

    // Capture local wake time before DST un-adjust (user-visible time)
    int wake_hour_local = alarm_hour;
    int wake_minute_local = alarm_minute;

    if (dst_active)
        alarm_hour = dst_adjust_hour(alarm_hour, false);

    if (active_rtc_backend == RTC_BACKEND_RV3028) {
        int rc = rv3028_set_daily_alarm_hour_minute(&rv3028_ctx, (uint8_t)alarm_hour,
                                                    (uint8_t)alarm_minute);
        if (rc != 0)
            debug_status("ERROR", "RV-3028 alarm setup failed (rc=%d)\n", rc);

        rc = rv3028_enable_alarm_interrupt(&rv3028_ctx, true);
        if (rc != 0)
            debug_status("ERROR", "RV-3028 alarm interrupt enable failed (rc=%d)\n", rc);

        rc = rv3028_clear_alarm_flag(&rv3028_ctx);
        if (rc != 0)
            debug_status("ERROR", "RV-3028 alarm flag clear failed (rc=%d)\n", rc);

        debug_log("RV-3028 alarm set for %02d:%02d (RTC time)\n", alarm_hour, alarm_minute);
    } else {
        ds3231_alarm_2_t alarm2 = {
            .minutes = alarm_minute, .hours = alarm_hour, .date = 0, .day = 0, .am_pm = false};

        ds3231_enable_alarm_interrupt(&ds3231_ctx, true);
        ds3231_set_alarm_2(&ds3231_ctx, &alarm2, ON_MATCHING_MINUTE_AND_HOUR);
        debug_log("Alarm2 set for %02d:%02d (RTC time)\n", alarm2.hours, alarm2.minutes);

        ds3231_clear_alarm2(&ds3231_ctx);
    }

    debug_status("OK", "Next wake: %02d:%02d (+%d min)\n", wake_hour_local, wake_minute_local,
                 refresh);
}

void rtc_disable_alarm(void) {
    if (active_rtc_backend == RTC_BACKEND_RV3028) {
        rv3028_enable_alarm_interrupt(&rv3028_ctx, false);
        rv3028_clear_alarm_flag(&rv3028_ctx);
    } else if (active_rtc_backend == RTC_BACKEND_DS3231) {
        ds3231_enable_alarm_interrupt(&ds3231_ctx, false);
        ds3231_clear_alarm2(&ds3231_ctx);
    }
}

static int64_t rtc_time_to_seconds(const rtc_time_t *t) {
    int64_t days = (int64_t)(t->year + 2000) * 365 + (t->year + 2000) / 4 + t->date;
    days += (int64_t)t->month * 30;
    return days * 86400 + (int64_t)t->hours * 3600 + (int64_t)t->minutes * 60 + t->seconds;
}

void rtc_sync_from_server(void) {
    if (!device_config_flash.data.autoset_rtc_enabled)
        return;
    if (active_rtc_backend == RTC_BACKEND_NONE)
        return;

    rtc_time_t server_utc = {0};
    if (!http_get_server_time(&server_utc)) {
        debug_status("WARN", "RTC sync: no server time available\n");
        return;
    }

    int hour_local = server_utc.hours + RTC_TIMEZONE_OFFSET_H;
    if (hour_local >= 24) {
        hour_local -= 24;
        server_utc.date += 1;
    }
    server_utc.hours = (uint8_t)hour_local;

    rtc_time_t rtc_now = {0};
    if (rtc_read_time(&rtc_now) != 0) {
        debug_status("ERROR", "RTC sync: read failed\n");
        return;
    }

    int64_t dev = rtc_time_to_seconds(&server_utc) - rtc_time_to_seconds(&rtc_now);
    if (dev < 0)
        dev = -dev;

    if (dev <= RTC_AUTOSET_THRESHOLD_S) {
        debug_status("OK", "RTC sync: in sync (dev %llds)\n", (long long)dev);
        return;
    }

    int rc = rtc_set_time(&server_utc);
    if (rc != 0) {
        debug_status("ERROR", "RTC sync: write failed (rc=%d)\n", rc);
        return;
    }

    debug_status("OK", "RTC sync: corrected (dev %llds) -> %02d:%02d:%02d\n", (long long)dev,
                 server_utc.hours, server_utc.minutes, server_utc.seconds);
}
