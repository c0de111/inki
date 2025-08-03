// rooms.c
#include "rooms.h"
#include "ImageResources.h"

// Image arrays and constants
const SubImage battery_levels_64x97[] = {
    { .data = gImage_battery_level_1, .width = 64, .height = 97 },
    { .data = gImage_battery_level_2, .width = 64, .height = 97 },
    { .data = gImage_battery_level_3, .width = 64, .height = 97 },
    { .data = gImage_battery_level_4, .width = 64, .height = 97 },
    { .data = gImage_battery_level_5, .width = 64, .height = 97 },
    { .data = gImage_battery_level_6, .width = 64, .height = 97 },
    { .data = gImage_battery_level_7, .width = 64, .height = 97 },
    { .data = gImage_battery_level_8, .width = 64, .height = 97 },
    { .data = gImage_battery_level_9, .width = 64, .height = 97 },
    { .data = gImage_battery_level_10, .width = 64, .height = 97 },
};

const SubImage eSign_128x128_white_background3 = {
    .data = gImage_eSign_128x128_white_background3,
    .width = 128,
    .height = 121
};

const SubImage eSign_100x100_3 = {
    .data = gImage_eSign_100x100_3,
    .width = 104,
    .height = 95
};

const SubImage qr_Seminarraum = {
    .data = gImage_qr_Seminarraum,
    .width = 96,
    .height = 90
};

const SubImage qr_github_link = {
    .data = gImage_github_link,
    .width = 56,
    .height = 50
};

// Default room configuration
// const RoomConfig DEFAULT_CAMPUS_H_CONFIG = {
    // .roomname = "Room",
    // .properties = {
    //     .type = ROOM_TYPE_OFFICE,
    //     .description = "Office Space",
    //     .number_of_seats = 3,
    //     .number_of_people_meeting = 3,
    //     .has_projector = false,
    //     .has_conferencesystem = false,
    // },
    // .epapertype = EPAPER_WAVESHARE_4IN2_V2,
    // .conversion_factor = 0.0017,
    // .refresh_minutes_by_pushbutton = {30, 30, 30, 30, 30, 30, 30, 30},
    // .wifi_reconnect_minutes = 5,
    // .watchdog_time = 8000,
    // .wifi_timeout = 4000,
    // .number_wifi_attempts = 6,
    // .max_wait_data_wifi = 100,
    // .pushbutton1_pin = 7,
    // .pushbutton2_pin = 6,
    // .pushbutton3_pin = 5,
    // .num_pushbuttons = 3,
    // .show_query_date = true,
    // .query_only_at_officehours = false,
    // .switch_off_battery_voltage = 2.7,
// };

// Active config, initially set to default – kann später überschrieben werden
// RoomConfig current_room = {};

// const RoomConfig* current_room = &DEFAULT_CAMPUS_H_CONFIG;
