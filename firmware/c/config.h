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

// Use case selection - can be overridden by build script (./build.sh --use-case ...)
// Default fallback if no build-time define is provided
#if !defined(USE_CASE_SEATSURFING) && \
    !defined(USE_CASE_HISTORIAN)  && \
    !defined(USE_CASE_HOMEMATIC)  && \
    !defined(USE_CASE_WEATHERMAP) && \
    !defined(USE_CASE_NEW_USECASE)
    #define USE_CASE_SEATSURFING    // Default fallback
#endif

// Validate use case selection: exactly one must be defined
#if defined(USE_CASE_SEATSURFING)
    #if defined(USE_CASE_HISTORIAN) || defined(USE_CASE_HOMEMATIC) || defined(USE_CASE_NEW_USECASE)
        #error "Define exactly one use case: USE_CASE_SEATSURFING, USE_CASE_HISTORIAN, USE_CASE_HOMEMATIC, or USE_CASE_NEW_USECASE."
    #endif
    #define USE_CASE_NAME "SeatSurfing"
#elif defined(USE_CASE_HISTORIAN)
    #if defined(USE_CASE_HOMEMATIC) || defined(USE_CASE_NEW_USECASE)
        #error "Define exactly one use case: USE_CASE_SEATSURFING, USE_CASE_HISTORIAN, USE_CASE_HOMEMATIC, or USE_CASE_NEW_USECASE."
    #endif
    #define USE_CASE_NAME "Historian"
#elif defined(USE_CASE_HOMEMATIC)
    #if defined(USE_CASE_NEW_USECASE)
        #error "Define exactly one use case: USE_CASE_SEATSURFING, USE_CASE_HISTORIAN, USE_CASE_HOMEMATIC, or USE_CASE_NEW_USECASE."
    #endif
    #define USE_CASE_NAME "Homematic"
#elif defined(USE_CASE_WEATHERMAP)
    #define USE_CASE_NAME "Weathermap"
#elif defined(USE_CASE_NEW_USECASE)
    #define USE_CASE_NAME "NewUseCase"
#else
    #error "No use case defined! Define USE_CASE_SEATSURFING, USE_CASE_HISTORIAN, USE_CASE_HOMEMATIC, USE_CASE_WEATHERMAP, or USE_CASE_NEW_USECASE."
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

// -----------------------------------------------------------------------------
// LED Configuration
// -----------------------------------------------------------------------------

// Use external front LED connected to a GPIO (1=enable, 0=disable)
#ifndef LED_USE_EXT
#define LED_USE_EXT 1
#endif

// GPIO number for external LED (default GP16)
#ifndef LED_EXT_GPIO
#define LED_EXT_GPIO 16
#endif

// Use Pico W onboard LED (1=enable, 0=disable)
// Note: Only usable after cyw43 is initialized (e.g., in Wi‑Fi setup)
#ifndef LED_USE_BOARD
#define LED_USE_BOARD 1
#endif

// Blink Morse code during Wi‑Fi setup (1=enable, 0=disable)
#ifndef LED_MORSE_ENABLED
#define LED_MORSE_ENABLED 1
#endif

// Morse timing unit in milliseconds
#ifndef LED_MORSE_UNIT_MS
#define LED_MORSE_UNIT_MS 150
#endif


/**
 * @brief Maximum lengths for Wi-Fi credentials.
 */
#define WIFI_SSID_MAX_LEN              32
#define WIFI_PASSWORD_MAX_LEN          64

#endif // CONFIG_H
