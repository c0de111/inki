/**
 * @file    ds3231.h
 * @author  Alper Tunga Güven (alperguven@std.iyte.edu.tr)
 * @brief   A driver library for DS3231 written for Raspberry Pi Pico.
 * Datasheet Link: https://www.analog.com/media/en/technical-documentation/data-sheets/DS3231.pdf
 * @version 0.1
 * @date    2023-08-12
 * 
 * @copyright Copyright (c) 2023
 * 
 */


#include "hardware/i2c.h"
#include "hardware/gpio.h"

#ifndef DS_3231
#define DS_3231

/* DS3231 Adress is fixed */
#define DS3231_DEVICE_ADRESS            0x68

/* DS231 modules comes with a AT24C32 EEPROM for logging and its
adress can be changed by soldering jumpers to A0, A1 and A2 inputs on the module. */
#define AT24C32_EEPROM_ADRESS_0         0x57    // Default
#define AT24C32_EEPROM_ADRESS_1         0x56    // A0
#define AT24C32_EEPROM_ADRESS_2         0x55    // A1
#define AT24C32_EEPROM_ADRESS_3         0x54    // A0 A1
#define AT24C32_EEPROM_ADRESS_4         0x53    // A2
#define AT24C32_EEPROM_ADRESS_5         0x52    // A2 A0
#define AT24C32_EEPROM_ADRESS_6         0x51    // A2 A1
#define AT24C32_EEPROM_ADRESS_7         0x50    // A2 A1 A0

#define AT24C32_PAGE_COUNT              256     
#define AT24C32_PAGE_SIZE               32      // Bytes

/* Timekeeping Registers */
#define DS3231_SECONDS_REG              0x00
#define DS3231_MINUTES_REG              0x01
#define DS3231_HOURS_REG                0x02
#define DS3231_DAY_REG                  0x03
#define DS3231_DATE_REG                 0x04
#define DS3231_MONTH_REG                0x05
#define DS3231_YEAR_REG                 0x06

#define DS3231_SECONDS_ALARM_1_REG      0x07
#define DS3231_MINUTES_ALARM_1_REG      0x08
#define DS3231_HOURS_ALARM_1_REG        0x09
#define DS3231_DAY_ALARM_1_REG          0x0A
#define DS3231_DATE_ALARM_1_REG         0x0A

#define DS3231_MINUTES_ALARM_2_REG      0x0B
#define DS3231_HOURS_ALARM_2_REG        0x0C
#define DS3231_DAY_ALARM_2_REG          0x0D
#define DS3231_DATE_ALARM_2_REG         0x0D

#define DS3231_CONTROL_REG              0x0E
#define DS3231_CONTROL_STATUS_REG       0x0F

#define DS3231_AGING_OFFSET_REG         0x10

#define DS3231_TEMPERATURE_MSB_REG      0x11
#define DS3231_TEMPERATURE_LSB_REG      0x12

enum days_of_week {
    MONDAY  = 1,
    TUESDAY,
    WEDNESDAY,
    THURSDAY,
    FRIDAY,
    SATURDAY,
    SUNDAY
};

enum ALARM_1_MASKS {
    ON_EVERY_SECOND = 0x0F,
    ON_MATCHING_SECOND = 0x0E,
    ON_MATCHING_SECOND_AND_MINUTE = 0x0C,
    ON_MATCHING_SECOND_MINUTE_AND_HOUR = 0x08,
    ON_MATCHING_SECOND_MINUTE_HOUR_AND_DATE = 0x00,
    ON_MATCHING_SECOND_MINUTE_HOUR_AND_DAY = 0x10
};

enum ALARM_2_MASKS {
    ON_EVERY_MINUTE = 0x07,
    ON_MATCHING_MINUTE = 0x06,
    ON_MATCHING_MINUTE_AND_HOUR = 0x05,
    ON_MATCHING_MINUTE_HOUR_AND_DATE = 0x00,
    ON_MATCHING_MINUTE_HOUR_AND_DAY = 0x01
};

enum SQUARE_WAVE_FREQUENCY {
    FREQUENCY_1_HZ = 0x0,
    FREQUENCY_1024_HZ = 0x1,
    FREQUENCY_4096_HZ = 0x2,
    FREQUENCY_8192_HZ = 0x3
};

/**
 * @brief Struct to hold hardware information about DS3231 and AT23C32 EEPROM.
 * 
 */
typedef struct ds3231_t {
    i2c_inst_t * i2c;
    uint8_t ds3231_addr;
    uint8_t at24c32_addr;
    bool am_pm_mode;
} ds3231_t;

/**
 * @brief Struct to hold time information received from DS3231.
 * 
 */
typedef struct ds3231_data_t {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    bool    am_pm;  // false if AM, true if PM.
    uint8_t day;
    uint8_t date;
    uint8_t month;
    uint8_t century;
    uint8_t year;
} ds3231_data_t;

/**
 * @brief Struct to hold alarm 1 information.
 * 
 */
typedef struct ds3231_alarm_1_t {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    bool am_pm;    // false if AM, true if PM.
    uint8_t day;
    uint8_t date;
} ds3231_alarm_1_t;

/**
 * @brief Struct to hold alarm 2 information.
 * 
 */
typedef struct ds3231_alarm_2_t {
    uint8_t minutes;
    uint8_t hours;
    bool am_pm;    // false if AM, true if PM.
    uint8_t day;
    uint8_t date;
} ds3231_alarm_2_t;

/* DS3231 Functions: */

int ds3231_init(ds3231_t * rtc, i2c_inst_t * i2c, uint8_t dev_addr, uint8_t eeprom_addr);
int ds3231_configure_time(ds3231_t * rtc, ds3231_data_t * data);

int ds3231_read_current_time(ds3231_t * rtc, ds3231_data_t * data);
int ds3231_read_temperature(ds3231_t * rtc, float * resolution);

int ds3231_set_alarm_1(ds3231_t * rtc, ds3231_alarm_1_t * alarm_time, enum ALARM_1_MASKS mask);
int ds3231_set_alarm_2(ds3231_t * rtc, ds3231_alarm_2_t * alarm_time, enum ALARM_2_MASKS mask);

int ds3231_enable_am_pm_mode(ds3231_t * rtc, bool enable);
int ds3231_enable_alarm_interrupt(ds3231_t * rtc, bool enable);
int ds3231_enable_oscillator(ds3231_t * rtc, bool enable);
int ds3231_enable_32khz_square_wave(ds3231_t * rtc, bool enable);
int ds3231_clear_alarm1(ds3231_t * rtc);
int ds3231_clear_alarm2(ds3231_t * rtc);
int ds3231_enable_battery_backed_square_wave(ds3231_t * rtc, bool enable);

int ds3231_check_oscillator_stop_flag(ds3231_t * rtc);
int ds3231_force_convert_temperature(ds3231_t * rtc);
int ds3231_set_square_wave_frequency(ds3231_t * rtc, enum SQUARE_WAVE_FREQUENCY sqr_frq);

int ds3231_set_aging_offset(ds3231_t * rtc, int8_t offset);

int ds3231_set_interrupt_callback_function(uint gpio, gpio_irq_callback_t callback);

/*--------------------------------------------------------------------------------------------------------*/

/* AT24C32 Functions: */

int at24c32_i2c_write_page(i2c_inst_t * i2c, uint8_t dev_addr, 
    uint8_t page_addr, uint8_t starting_byte, size_t length, uint8_t * data);

int at24c32_i2c_read_page(i2c_inst_t * i2c, uint8_t dev_addr, 
    uint8_t page_addr, uint8_t starting_byte, size_t length, uint8_t * data);

int at24c32_read_current_adress(i2c_inst_t * i2c, uint8_t dev_addr,
    size_t length, uint8_t * data);

int at24c32_write_current_time(ds3231_t * rtc, uint8_t page_addr);

#endif
