/**
 * @file debug.h
 * @brief Provides debug logging functionality with configurable verbosity and output modes.
 *
 * This module enables flexible debugging capabilities, including:
 * - Real-time logging to the console.
 * - Buffered logging for deferred transmission.
 * - Advanced formatting features like colored and timestamped messages.
 *
 * The logging system is highly configurable, allowing developers to choose the
 * appropriate mode for their debugging needs:
 * - `DEBUG_NONE`: Suppresses all debug output.
 * - `DEBUG_REALTIME`: Outputs messages to the console as they are logged.
 * - `DEBUG_BUFFERED`: Stores messages in a buffer for later transmission.
 * - `DEBUG_BOTH`: Combines real-time console output and buffered storage.
 *
 * The module also supports ANSI escape codes for colored text output, enhancing
 * readability of logs during debugging sessions.
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h> // For standard data type definitions

/**
 * @def DEBUG_BUFFER_SIZE
 * @brief The size of the buffer used for storing debug messages.
 */
#define DEBUG_BUFFER_SIZE 4096

/**
 * @enum DebugMode
 * @brief Represents the available debug output modes.
 *
 * - `DEBUG_NONE`: No debug output is generated.
 * - `DEBUG_REALTIME`: Messages are logged to the console in real-time using `printf`.
 * - `DEBUG_BUFFERED`: Messages are buffered and transmitted later.
 * - `DEBUG_BOTH`: Combines real-time logging with buffered storage.
 */
typedef enum {
    DEBUG_NONE,      /**< No debug output */
    DEBUG_REALTIME,  /**< Real-time debug output using `printf` */
    DEBUG_BUFFERED,  /**< Buffered debug output for later transmission */
    DEBUG_BOTH       /**< Combination of real-time and buffered debug output */
} DebugMode;

/**
 * @brief Initializes the debug logging system.
 *
 * Resets the debug buffer and sets the default debug mode to `DEBUG_NONE`.
 * This function should be called during system initialization.
 */
void init_debug(void);

/**
 * @brief Sets the debug output mode.
 *
 * Configures the logging system to one of the available debug modes:
 * real-time, buffered, both, or none.
 *
 * @param mode The desired debug output mode (see `DebugMode`).
 */
void set_debug_mode(DebugMode mode);

/**
 * @brief Logs a debug message with optional formatting.
 *
 * Outputs a formatted message to the console, stores it in the buffer, or both,
 * depending on the current debug mode. All messages include a timestamp in
 * milliseconds since system start.
 *
 * @param format The format string, similar to `printf`.
 * @param ... Arguments corresponding to the format string.
 */
void debug_log(const char* format, ...);

/**
 * @brief Logs a debug message with color and formatting.
 *
 * Outputs a timestamped and colored message to the console, stores it in the
 * buffer, or both, depending on the current debug mode. Color is applied using
 * ANSI escape codes.
 *
 * @param color_code The ANSI escape code for the desired color (e.g., `"\033[31m"` for red).
 * @param format The format string, similar to `printf`.
 * @param ... Arguments corresponding to the format string.
 */
void debug_log_with_color(const char* color_code, const char* format, ...);

/**
 * @brief Transmits all buffered debug messages.
 *
 * Outputs all stored messages in the debug buffer to the console. Typically
 * used before system shutdown or as part of a periodic diagnostic routine.
 * The buffer is cleared after transmission.
 */
void transmit_debug_logs(void);

/**
 * @name ANSI Color Codes
 * @brief ANSI escape codes for text color and formatting.
 *
 * These codes can be used to enhance the readability of debug messages by
 * adding color or bold formatting.
 * @{
 */
#define COLOR_RESET "\033[0m"          /**< Resets all text formatting to default */

#define COLOR_RED "\033[31m"           /**< Sets text color to red */
#define COLOR_GREEN "\033[32m"         /**< Sets text color to green */
#define COLOR_YELLOW "\033[33m"        /**< Sets text color to yellow */
#define COLOR_BLUE "\033[34m"          /**< Sets text color to blue */
#define COLOR_MAGENTA "\033[35m"       /**< Sets text color to magenta */
#define COLOR_CYAN "\033[36m"          /**< Sets text color to cyan */
#define COLOR_WHITE "\033[37m"         /**< Sets text color to white */

#define COLOR_BOLD_RED "\033[1;31m"    /**< Sets text to bold red */
#define COLOR_BOLD_GREEN "\033[1;32m"  /**< Sets text to bold green */
#define COLOR_BOLD_YELLOW "\033[1;33m" /**< Sets text to bold yellow */
#define COLOR_BOLD_BLUE "\033[1;34m"   /**< Sets text to bold blue */
#define COLOR_BOLD_MAGENTA "\033[1;35m"/**< Sets text to bold magenta */
#define COLOR_BOLD_CYAN "\033[1;36m"   /**< Sets text to bold cyan */
#define COLOR_BOLD_WHITE "\033[1;37m"  /**< Sets text to bold white */
/** @} */

#endif // DEBUG_H
