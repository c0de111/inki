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
 *
 */

#ifndef ROOMS_H
#define ROOMS_H
#include "ImageResources.h"
#include "seatsurfing_api.h"

// Define ePaper types
typedef enum {
    EPAPER_NONE = 0,         // No ePaper used
    EPAPER_WAVESHARE_7IN5_V2, // Waveshare EPD_7in5_V2
    EPAPER_WAVESHARE_4IN2_V2, // Waveshare EPD_4in2
    EPAPER_WAVESHARE_2IN9_V2
} EpaperType;

// Define Room Types
typedef enum {
    ROOM_TYPE_OFFICE,           // Office space
    ROOM_TYPE_CONFERENCE,      // Conference room
    ROOM_TYPE_LAB,          // for future use
    ROOM_TYPE_WORKSHOP   // for future use
} RoomType;

typedef struct {
    RoomType type;
    const char* description;
    int number_of_seats;   // number of single-seat workspaces
    int number_of_people_meeting; // number of peple that can meet and discuss in the room
    bool has_projector;     // for larger groups
    bool has_conferencesystem;   // for videoconferencing
} RoomTypeProperties;

typedef struct {
    int x;       // Top-left corner x-coordinate
    int y;       // Top-left corner y-coordinate
    int width;   // Width in pixels
    int height;  // Height in pixels
} QRCodeConfig;

/**
 * @brief Represents a sub-image that can be drawn onto the ePaper buffer.
 *
 * This structure contains a pointer to the image data and its dimensions.
 */
typedef struct {
    const unsigned char* data; /**< Pointer to the sub-image pixel data. */
    int width;                 /**< Width of the sub-image in pixels. */
    int height;                /**< Height of the sub-image in pixels. */
} SubImage;

/**
 * @brief Enum for accessing elements of SubImage battery_levels_64x97
 */
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

/**
 * @brief Array of sub-images for battery levels (64x97 pixels).
 */
const SubImage battery_levels_64x97[] = {
    { .data = gImage_battery_level_1, .width = 64, .height = 97 },
    { .data = gImage_battery_level_2, .width = 64, .height = 97 },
    { .data = gImage_battery_level_3, .width = 64, .height = 97 },
    { .data = gImage_battery_level_4, .width = 64, .height = 97 },
    { .data = gImage_battery_level_5, .width = 64, .height = 97 },
    { .data = gImage_battery_level_6, .width = 64, .height = 97 }, // access by battery_levels_64x97[BATTERY_LEVEL_6]
    { .data = gImage_battery_level_7, .width = 64, .height = 97 },
    { .data = gImage_battery_level_8, .width = 64, .height = 97 },
    { .data = gImage_battery_level_9, .width = 64, .height = 97 },
    { .data = gImage_battery_level_10, .width = 64, .height = 97 },
};


/**
 * @brief Sub-image definition for the generated image (128x128 pixels).
 */
const SubImage eSign_128x128_white_background = {
    .data = gImage_eSign_128x128_white_background,
    .width = 128,
    .height = 128
};

/**
 * @brief Sub-image definition for the generated image (112x107 pixels).
 */
const SubImage generic_logo_112_107 = {
    .data = gImage_generic_logo_112_107,
    .width = 112,
    .height = 107
};

/**
 * @brief Sub-image definition for the generated image (96x90 pixels).
 */
const SubImage qr_Seminarraum = {
    .data = gImage_qr_Seminarraum,
    .width = 96,
    .height = 90
};

/**
 * @brief Sub-image definition for the generated image (104x99 pixels).
 */
const SubImage qr_github_link = {
    .data = gImage_github_link,
    .width = 56,
    .height = 50
};

/// @brief Structure representing a voltage interval.
typedef struct {
    int group_value;     ///< Percentage value (e.g., 10, 20, ..., 100).
    float voltage_min;   ///< Minimum voltage for the interval.
    float voltage_max;   ///< Maximum voltage for the interval.
} VoltageInterval;

// Lookup Table for RoomType Default Properties
static const RoomTypeProperties default_room_properties[] = {
    {ROOM_TYPE_OFFICE, "Office Space", 3, 3, false, false},        // Office: 3 seat, no projector, no conferencing
    {ROOM_TYPE_CONFERENCE, "Conference Room", 0, 20, true, true}, // Conference: 20 people, projector, conferencing
    {ROOM_TYPE_LAB, "Laboratory", 0, 5, false, false},            // Lab: Placeholder
    {ROOM_TYPE_WORKSHOP, "Workshop", 0, 3, false, false}         // Workshop: Placeholder
};

/**
 * Represents the configuration for a specific room ie an esign.
 */
typedef struct {
    const char* roomname;                /**< Name of the room (e.g., "LPB113H"). */
    RoomTypeProperties properties;       /**< Detailed properties of the room type. */
    EpaperType epapertype;               /**< Type of ePaper display (e.g., Waveshare EPD_7in5_V2). */
    const unsigned char* backgroundimage;/**< Pointer to the background image for the ePaper, might contain QR codes that can be overwritten. */
    SubImage qr_code_1_image;            /**< Pointer to the first QR code image. */
    SubImage qr_code_2_image;            /**< Pointer to the second QR code image. */
    SubImage qr_code_3_image;            /**< Pointer to the third QR code image. */
    float conversion_factor;             /**< Conversion factor for battery voltage, ADC. */
    // int refresh_minutes;                 /**< Interval (in minutes) to refresh the ePaper. */
    int refresh_minutes_by_pushbutton[8];
    int wifi_reconnect_minutes;          /**< Interval (in minutes) to reconnect Wi-Fi after connection failure. */
    int watchdog_time;                   /**< Duration of the watchdog timer (in seconds). */
    int wifi_timeout;                    /**< Timeout for Wi-Fi connection attempts (in ms). */
    int number_wifi_attempts;            /**< Number of attempts for Wi-Fi connection. */
    int max_wait_data_wifi;              /**< Maximum wait time for server response (in multiples of 50 ms). */
    uint8_t pushbutton1_pin;             /**< GPIO pin assigned to pushbutton 1, specific for board. */
    uint8_t pushbutton2_pin;             /**< GPIO pin assigned to pushbutton 2, specific for board. */
    uint8_t pushbutton3_pin;             /**< GPIO pin assigned to pushbutton 3, specific for board. */
    int num_pushbuttons;                 /**< Number of configured pushbuttons. */
    bool show_query_date;                /**< Whether to display the last query date on the ePaper. */
    bool query_only_at_officehours;      /**< Whether to operate only during office hours to save power. */
    float switch_off_battery_voltage;    /**< Below this battery voltage, display "low battery" and disable alarm -> no wake-up of circuit */
    int ip[4];                           /**< Server IP address (IPv4 format). */
    int port;                            /**< Server port number. */

    const char* host;
    const char* space_id;
    const char* location_id;
    const char* api_user;
} RoomConfig;

/**
 * Global default configuration for any room.
 *
 * This structure serves as a baseline for all room-specific configurations.
 * Individual rooms will copy this configuration and override specific fields.
 *
 * Related Enums:
 * - `RoomType`: Specifies the general type of the room (e.g., office, conference room), and related properties.
 * - `EpaperType`: Specifies the type of ePaper display used, and related properties.
 *
 * Default Settings:
 * - Room properties such as seating capacity, meeting capacity, and hardware capabilities (e.g., projector).
 * - Esign: Network settings (e.g., WiFi credentials, IP address, and port).
 * - Esign: Hardware-specific parameters like GPIO pin assignments and ADC conversion factors.
 */
/**

/**
* Default configuration for Campus H.
*/
const RoomConfig DEFAULT_CAMPUS_H_CONFIG = {
    .roomname = "Default Room - Campus H",
    .properties = {
        .type = ROOM_TYPE_OFFICE,
        .description = "Office Space (Campus H)",
        .number_of_seats = 3,
        .number_of_people_meeting = 3,
        .has_projector = false,
        .has_conferencesystem = false,
    },
    .epapertype = EPAPER_WAVESHARE_7IN5_V2,
    .conversion_factor = 0.0017,
    .refresh_minutes_by_pushbutton = {30, 30, 30, 30, 30, 30, 30, 30}, // [0] default, [1] pb1, [2] pb2, [4] pb3, ...
    .wifi_reconnect_minutes = 5,
    .watchdog_time = 8000,
    .wifi_timeout = 4000,
    .number_wifi_attempts = 6,
    .max_wait_data_wifi = 100, // how long to wait for the server to respond (wifi is already working) in multiples of 50 ms
    .pushbutton1_pin = 7,
    .pushbutton2_pin = 6,
    .pushbutton3_pin = 5,
    .num_pushbuttons = 3, //  Number of pushbuttons in use
    .show_query_date = true,
    .query_only_at_officehours = false, // only query at office hours
    .switch_off_battery_voltage = 2.7, // for alkali batteries, for future use
};

#ifdef ROOM113H
RoomConfig ROOM113H_config = {
    .roomname = "ROOM113H",
    .backgroundimage = gImage_generic_logo_112_107,
    .properties = DEFAULT_CAMPUS_H_CONFIG.properties,  // Inherit properties
    .epapertype = EPAPER_WAVESHARE_7IN5_V2, // ePaper type
    .conversion_factor = DEFAULT_CAMPUS_H_CONFIG.conversion_factor,
    .refresh_minutes_by_pushbutton = {30, 30, 30, 30, 30, 30, 30, 30}, // [0] default, [1] pb1, [2] pb2, [4] pb3, ...
    .wifi_reconnect_minutes = DEFAULT_CAMPUS_H_CONFIG.wifi_reconnect_minutes,
    .watchdog_time = DEFAULT_CAMPUS_H_CONFIG.watchdog_time,
    .wifi_timeout = DEFAULT_CAMPUS_H_CONFIG.wifi_timeout,
    .number_wifi_attempts = DEFAULT_CAMPUS_H_CONFIG.number_wifi_attempts,
    .max_wait_data_wifi = DEFAULT_CAMPUS_H_CONFIG.max_wait_data_wifi,
    .pushbutton1_pin = DEFAULT_CAMPUS_H_CONFIG.pushbutton1_pin,
    .pushbutton2_pin = DEFAULT_CAMPUS_H_CONFIG.pushbutton2_pin,
    .pushbutton3_pin = DEFAULT_CAMPUS_H_CONFIG.pushbutton3_pin,
    .num_pushbuttons = DEFAULT_CAMPUS_H_CONFIG.num_pushbuttons,
    .show_query_date = DEFAULT_CAMPUS_H_CONFIG.show_query_date,
    .query_only_at_officehours = false,
    .ip = API_IP,
    .port = API_PORT,
    .space_id = "sid", // seatsurfing space_id, can be found in booking link in "Areas"
    .location_id = "lid", // seatsurfing location_id, can be found in booking link in "Areas"
};

const RoomConfig* current_room = &ROOM113H_config;

#elif defined(ROOM102H)
/* Configuration for ROOM102H room in Campus H.
 * Uses default Campus H settings and overrides only the room name. */
RoomConfig ROOM102H_config = {
    .roomname = "ROOM102H",
    .backgroundimage = gImage_generic_logo_112_107,
    .properties = DEFAULT_CAMPUS_H_CONFIG.properties,  // Inherit properties
    .epapertype = EPAPER_WAVESHARE_4IN2_V2, // ePaper type
    .conversion_factor = DEFAULT_CAMPUS_H_CONFIG.conversion_factor,
    .refresh_minutes_by_pushbutton = {30, 30, 30, 30, 30, 30, 30, 30}, // [0] default, [1] pb1, [2] pb2, [4] pb3, ...
    .wifi_reconnect_minutes = DEFAULT_CAMPUS_H_CONFIG.wifi_reconnect_minutes,
    .watchdog_time = DEFAULT_CAMPUS_H_CONFIG.watchdog_time,
    .wifi_timeout = DEFAULT_CAMPUS_H_CONFIG.wifi_timeout,
    .number_wifi_attempts = DEFAULT_CAMPUS_H_CONFIG.number_wifi_attempts, // times 50 ms
    .max_wait_data_wifi = DEFAULT_CAMPUS_H_CONFIG.max_wait_data_wifi,
    .pushbutton1_pin = DEFAULT_CAMPUS_H_CONFIG.pushbutton1_pin,
    .pushbutton2_pin = DEFAULT_CAMPUS_H_CONFIG.pushbutton2_pin,
    .pushbutton3_pin = DEFAULT_CAMPUS_H_CONFIG.pushbutton3_pin,
    .num_pushbuttons = DEFAULT_CAMPUS_H_CONFIG.num_pushbuttons,
    .show_query_date = false, //DEFAULT_CAMPUS_H_CONFIG.show_query_date,
    .query_only_at_officehours = true, //DEFAULT_CAMPUS_H_CONFIG.query_only_at_officehours,
    .switch_off_battery_voltage = 3.2,
    .ip = API_IP,
    .port = API_PORT,
    .space_id = "8bd3f488-d2d8-42db-b973-1624715164a0", // seatsurfing space_id, can be found in booking link in "Areas"
    .location_id = "5230035c-94ce-4f3c-b112-ebc6afcb78b9" // seatsurfing location_id, can be found in booking link in "Areas"
};

const RoomConfig* current_room = &ROOM102H_config;

#else
#error "No room configuration defined! Please define one of LPB113H or LPB111H."
#endif

#endif // ROOMS_H


