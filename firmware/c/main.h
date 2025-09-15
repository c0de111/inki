// main.h
#ifndef MAIN_H
#define MAIN_H

#include "ds3231.h"  // oder der Pfad zu deiner RTC-Struktur
#include "DEV_Config.h"    // For device configuration
#include <stdint.h>

extern ds3231_t rtc;
// extern const RoomConfig* current_room;

void set_rtc_from_display_string(ds3231_t* ds3231, const char* line);
void set_alarmclock_and_powerdown(ds3231_t* clock);
void epaper_finalize_and_powerdown(UBYTE* image);
void render_4gray_test_pattern(void);
void read_mac_address();
float read_battery_voltage(float conversion_factor);
float read_coin_cell_voltage(float conversion_factor);
float read_onchip_temperature_c(void);
float read_ds3231_temperature_c(void);
typedef struct memory_info_t {
    size_t heap_used_bytes;      // exact at call time
    size_t heap_headroom_bytes;  // exact at call time
    size_t stack_margin_bytes;   // approx (core0 snapshot)
    uintptr_t heap_base_addr;    // &__bss_end__
    uintptr_t heap_end_addr;     // sbrk(0)
    uintptr_t heap_limit_addr;   // &__StackLimit
    uintptr_t sp_addr;           // current core0 SP snapshot
} memory_info_t;

void get_memory_info(memory_info_t* out);
void format_rtc_time(const ds3231_data_t* t, char* buffer, size_t buffer_size);
const char* get_day_of_week(int day);
const char* get_month_name(int month);


UBYTE* init_epaper();


#endif
