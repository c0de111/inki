#include "flash.h"
#include "wifi_config.h"
#include "seatsurfing_config.h"
#include "device_config.h"

__attribute__((section(".wifi_config")))
__attribute__((used))
const wifi_config_t default_wifi_config = {
    .ssid = "default_ssid",
    .password = "default_password",
    .crc32 = 0
};

// Seatsurfing Konfiguration
__attribute__((section(".seatsurfing_config")))
__attribute__((used))
const seatsurfing_config_t default_seatsurfing_config = {
    .data = {
        .host = "seatsurfing.io",
        .ip = {192,168,178,85},
        .port = 8080,
        .username = "default_esign@seatsurfing.local",
        .password = "default_password",
        .space_id = "default_space_id",
        .location_id = "default_location_id"
    },
    .crc32 = 0
};

// Geräte-/UI-Konfiguration
__attribute__((section(".device_config")))
__attribute__((used))
const device_config_t default_device_config = {
    .data = {
        .roomname = "Room 204",
        .type = ROOM_TYPE_OFFICE,
        .epapertype = EPAPER_WAVESHARE_7IN5_V2, //EPAPER_WAVESHARE_4IN2_V2,
        .refresh_minutes_by_pushbutton = {30, 30, 30, 30, 30, 30, 30, 30},
        .show_query_date = true,
        .query_only_at_officehours = false,
        .switch_off_battery_voltage = 2.7,
        .description = "Office Space",
        .number_of_seats = 1,
        .number_of_people_meeting = 1,
        .has_projector = false,
        .has_conferencesystem = false,
        .conversion_factor = 0.00169,
        .wifi_reconnect_minutes = 5,
        .watchdog_time = 8000,
        .wifi_timeout = 5000,
        .number_wifi_attempts = 6,
        .max_wait_data_wifi = 100,
        .pushbutton1_pin = 7,
        .pushbutton2_pin = 6,
        .pushbutton3_pin = 5,
        .num_pushbuttons = 3
    },
    .crc32 = 0
};
