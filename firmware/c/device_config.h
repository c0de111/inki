#pragma once
#include <stdint.h>
#include "rooms.h"

#define USER_CONFIG_MAX_SIZE 4096

typedef struct {
    char roomname[16];
    RoomType type;
    EpaperType epapertype;
    int refresh_minutes_by_pushbutton[8];
    bool show_query_date;
    bool query_only_at_officehours;
    float switch_off_battery_voltage;
    char description[32];
    int number_of_seats;
    int number_of_people_meeting;
    bool has_projector;
    bool has_conferencesystem;
    float conversion_factor;
    int wifi_reconnect_minutes;
    int watchdog_time;
    int wifi_timeout;
    int number_wifi_attempts;
    int max_wait_data_wifi;
    uint8_t pushbutton1_pin;
    uint8_t pushbutton2_pin;
    uint8_t pushbutton3_pin;
    int num_pushbuttons;
    // uint8_t background_id;  // keine Pointer im Flash!
    // SubImage qr_code_1_image;
    // SubImage qr_code_2_image;
    // SubImage qr_code_3_image;
} device_config_data_t;

typedef struct {
    device_config_data_t data;
    uint32_t crc32;
} device_config_t;

