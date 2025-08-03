// main.h
#ifndef MAIN_H
#define MAIN_H

#include "ds3231.h"  // oder der Pfad zu deiner RTC-Struktur
#include "rooms.h"
#include "DEV_Config.h"    // For device configuration

extern ds3231_t rtc;
// extern const RoomConfig* current_room;

void set_rtc_from_display_string(ds3231_t* ds3231, const char* line);
void set_alarmclock_and_powerdown(ds3231_t* clock);
void epaper_finalize_and_powerdown(UBYTE* image);
void read_mac_address();
float read_battery_voltage(float conversion_factor);
float read_coin_cell_voltage(float conversion_factor);
void format_rtc_time(const ds3231_data_t* t, char* buffer, size_t buffer_size);
const char* get_day_of_week(int day);
const char* get_month_name(int month);


UBYTE* init_epaper();


#endif
