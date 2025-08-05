#pragma once
#include <stdint.h>
#include "ImageResources.h"

#define USER_CONFIG_MAX_SIZE 4096

// Enum for ePaper types
typedef enum {
    EPAPER_NONE = 0,
    EPAPER_WAVESHARE_7IN5_V2,
    EPAPER_WAVESHARE_4IN2_V2,
    EPAPER_WAVESHARE_2IN9_V2
} EpaperType;

// Enum for room usage type
typedef enum {
    ROOM_TYPE_OFFICE,
    ROOM_TYPE_CONFERENCE,
    ROOM_TYPE_LAB,
    ROOM_TYPE_WORKSHOP
} RoomType;

// Properties describing a room type
typedef struct {
    RoomType type;
    const char* description;
    int number_of_seats;
    int number_of_people_meeting;
    bool has_projector;
    bool has_conferencesystem;
} RoomTypeProperties;

// Battery level index enum
typedef enum {
    BATTERY_LEVEL_1,
    BATTERY_LEVEL_2,
    BATTERY_LEVEL_3,
    BATTERY_LEVEL_4,
    BATTERY_LEVEL_5,
    BATTERY_LEVEL_6,
    BATTERY_LEVEL_7,
    BATTERY_LEVEL_8,
    BATTERY_LEVEL_9,
    BATTERY_LEVEL_10,
    BATTERY_LEVEL_COUNT
} BatteryLevelIndex;

// Voltage range grouped by percentage
typedef struct {
    int group_value;
    float voltage_min;
    float voltage_max;
} VoltageInterval;

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

