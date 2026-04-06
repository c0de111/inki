#include "pico/stdlib.h"
#include "ds3231.h"
#include <stdio.h>

/* A basic callback function that triggers when an alarm triggers. */
void ds3231_interrupt_callback(uint gpio, uint32_t event_mask) {
    printf("Alarm Enabled\n");
}

int main() {
    /* Initilize serial communicatio.n */
    stdio_init_all();
    /* Define pins used for DS3231. */
    uint8_t sda_pin = 12; 
    uint8_t scl_pin = 13;
    uint8_t int_pin = 18; 

    const char * days[7] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};

    /* Set your current time. */
    ds3231_data_t ds3231_data = {
        .seconds = 25,
        .minutes = 23,
        .hours = 23,
        .day = 4,
        .date = 10,
        .month = 8,
        .year = 23,
        .century = 1,
        .am_pm = false
    };
    ds3231_t ds3231;

    /* Set the desired alarm time */
    ds3231_alarm_1_t alarm = {
        .seconds = 7,
        .minutes = 25,
        .hours = 0,
        .date = 0,
        .day = 0,
        .am_pm = false
    };

    /* Initiliaze ds3231 struct. */
    ds3231_init(&ds3231, i2c_default, DS3231_DEVICE_ADRESS, AT24C32_EEPROM_ADRESS_0);

    sleep_ms(200);

    /* Initiliaze on-board LED to blink every second to test if the board is working correctly. */
    #ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, 1);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    #endif

    /* Initiliaze I2C line. */
    gpio_init(sda_pin);
    gpio_init(scl_pin);
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);
    i2c_init(ds3231.i2c, 400 * 1000);

    /* Update the DS3231 time registers with the desired time and set alarm 1 to send interrupt signal. */
    ds3231_configure_time(&ds3231, &ds3231_data);
    ds3231_set_alarm_1(&ds3231, &alarm, ON_MATCHING_SECOND_AND_MINUTE);
    ds3231_set_interrupt_callback_function(int_pin, &ds3231_interrupt_callback);

    sleep_ms(1000);
    printf("Starting Loop:\n");

    while(true) {
        /* Read the time registers of DS3231. */
        if(ds3231_read_current_time(&ds3231, &ds3231_data)) {
            printf("No data is received\n");
        } else {
            printf("%02u:%02u:%02u    %10s    %02u/%02u/20%02u\n", 
                ds3231_data.hours, ds3231_data.minutes, ds3231_data.seconds, 
                days[ds3231_data.day - 1], ds3231_data.date, ds3231_data.month, ds3231_data.year);
        }
        #ifdef PICO_DEFAULT_LED_PIN
        gpio_put(25, 0);
        sleep_ms(500);
        gpio_put(25, 1);
        sleep_ms(500);
        #endif
    }
}