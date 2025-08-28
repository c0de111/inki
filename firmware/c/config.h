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
// #include "rooms.h"
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// USE CASE SELECTION - Build-Time Configuration
// =============================================================================

/**
 * @brief Use case selection for inki firmware.
 * 
 * Uncomment exactly ONE of the following use cases to build firmware for:
 * - USE_CASE_SEATSURFING: Room booking and availability display (default)
 * - USE_CASE_HISTORIAN: Time-series data visualization and monitoring
 * 
 * The selected use case determines:
 * - Which communication protocols are used
 * - What configuration pages are shown in setup mode
 * - How data is parsed and displayed on the ePaper
 * - Which configuration structures are included
 */

// Use case selection - can be overridden by build script (./build.sh --historian)
// Default fallback if no build-time define is provided
#ifndef USE_CASE_SEATSURFING
    #ifndef USE_CASE_HISTORIAN
        #ifndef USE_CASE_NEW_USECASE
            #define USE_CASE_SEATSURFING    // Default fallback
        #endif
    #endif
#endif

// Validate use case selection
#ifdef USE_CASE_SEATSURFING
    #ifdef USE_CASE_HISTORIAN
        #error "Cannot define both USE_CASE_SEATSURFING and USE_CASE_HISTORIAN! Please choose exactly one."
    #endif
    #define USE_CASE_NAME "SeatSurfing"
    #define USE_CASE_DESCRIPTION "Room Booking & Availability Display"
#elif defined(USE_CASE_HISTORIAN)
    #define USE_CASE_NAME "Historian"
    #define USE_CASE_DESCRIPTION "Time-Series Data Visualization"
#else
    #error "No use case defined! Please define either USE_CASE_SEATSURFING or USE_CASE_HISTORIAN in config.h"
#endif

#define WIFI_SETUP_TIMEOUT_MS (15 * 60 * 1000)

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


/**
 * @brief Maximum lengths for Wi-Fi credentials.
 */
#define WIFI_SSID_MAX_LEN              32
#define WIFI_PASSWORD_MAX_LEN          64

#endif // CONFIG_H
