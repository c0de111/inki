/**
 * @file    rooms.h
 * @brief   Defines room configurations and properties as relevant for use in the esign.
 *
 * This header provides the definitions and data structures used to configure
 * room-specific properties for the ePaper-based room management system. The
 * structures include configuration details for room types, ePaper display
 * settings, QR code positioning, and Wi-Fi network parameters.
 *
 * @details
 * - The `RoomConfig` structure encapsulates all the configurable parameters
 *   for a room.
 * - Supports multiple ePaper types and customizable layouts.
 * - Includes QR code configurations for up to three codes per room.
 */

#ifndef ROOMS_H
#define ROOMS_H

#include "ImageResources.h"
#include "seatsurfing_api.h"
#include <stdint.h>
#include <stdbool.h>

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

// Structure for a rectangular QR code placement
typedef struct {
    int x;
    int y;
    int width;
    int height;
} QRCodeConfig;

// Structure for image data used on display
typedef struct {
    const unsigned char* data;
    int width;
    int height;
} SubImage;

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

// Room configuration struct
// typedef struct {
    // const char* roomname;
    // RoomTypeProperties properties;
    // EpaperType epapertype;
    // const unsigned char* backgroundimage;
    // SubImage qr_code_1_image;
    // SubImage qr_code_2_image;
    // SubImage qr_code_3_image;
    // float conversion_factor;
    // int refresh_minutes_by_pushbutton[8];
    // int wifi_reconnect_minutes;
    // int watchdog_time;
    // int wifi_timeout;
    // int number_wifi_attempts;
    // int max_wait_data_wifi;
    // uint8_t pushbutton1_pin;
    // uint8_t pushbutton2_pin;
    // uint8_t pushbutton3_pin;
    // int num_pushbuttons;
    // bool show_query_date;
    // bool query_only_at_officehours;
    // float switch_off_battery_voltage;
    // int ip[4];
    // int port;
    // const char* host;
    // const char* space_id;
    // const char* location_id;
    // const char* api_user;
// } RoomConfig;

// Image assets declared externally
extern const SubImage battery_levels_64x97[];
extern const SubImage eSign_128x128_white_background3;
extern const SubImage eSign_100x100_3;
extern const SubImage qr_Seminarraum;
extern const SubImage qr_github_link;

// Default config instance declared externally
// extern const RoomConfig DEFAULT_CAMPUS_H_CONFIG;

// Active room config instance (used globally)
// extern const RoomConfig* current_room;
#endif // ROOMS_H

// const RoomConfig* current_room = &DEFAULT_CAMPUS_H_CONFIG;
