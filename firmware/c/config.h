/**
 * @file    config.h
 * @brief   General configuration constants and macros for the eSign project.
 *
 * This file contains globally used constants, macros, and definitions
 * for configuring system-wide parameters such as pin assignments,
 * hardware capabilities, and project-specific settings.
 */

#ifndef CONFIG_H
#define CONFIG_H

// Include rooms.h, which defines RoomConfig
#include "rooms.h"

// -----------------------------------------------------------------------------
// Booking fetch method selection
// -----------------------------------------------------------------------------

/**
 * @brief Choose which method to use for fetching booking data.
 *
 * Options:
 * - 0: Use legacy Bash+CGI script
 * - 1: Use new REST API endpoint (preferred for modern installations)
 */
#define USE_SEATSURFING_API 1


// -----------------------------------------------------------------------------
// Timezone and DST configuration
// -----------------------------------------------------------------------------

/**
 * @brief The base timezone offset in hours from UTC.
 *
 * This should reflect the standard (non-DST) timezone, e.g.:
 * - Central Europe (MEZ): +1
 * - UK/Ireland: 0
 * - US Eastern Standard Time (EST): -5
 */
#define TIMEZONE_OFFSET_HOURS (+1)

/**
 * @brief Enable DST support with a region-specific rule.
 *
 * Define one of the following to enable DST calculations.
 * You may later extend this with other rules like USE_DST_US, USE_DST_AU, etc.
 */
#define USE_DST_EUROPE


#define DS3231_SDA_PIN 20
#define DS3231_SCL_PIN 21
#define I2C_FREQ 400*1000 //max at 400 kHz
#define GATE_PIN 22 //GATE PIN, MOSFET for power supply control

#define EPAPER_ON

#define QR_ENABLED
// #define BATTERY_STATUS
#define HIGH_VERBOSE_DEBUG

/*
 * Global pointer to the current room configuration.
 *
 * The `current_room` pointer is set at compile time to point to the configuration
 * for the specific room being compiled (e.g., LPB113H_config, LPB111H_config).
 * This allows the main program and other parts of the system to use a unified
 * interface (`current_room->field`) regardless of the room being compiled.
 */

extern const RoomConfig* current_room;

#endif // CONFIG_H
