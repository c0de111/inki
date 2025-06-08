/**
 * @file debug.c
 * @brief Debug logging routines for the eSign project.
 *
 * This file implements flexible debugging capabilities for the eSign project,
 * including real-time (caveat: via USB this is not reliable!) console output, buffered logging for later transmission,
 * and support for timestamps in milliseconds since system start. Messages can
 * include ANSI escape codes for enhanced readability, such as bold and colored text.
 *
 * The logging mechanism supports the following modes:
 * - `DEBUG_NONE`: No debug output.
 * - `DEBUG_REALTIME`: Logs messages to the console in real-time using `printf`.
 * - `DEBUG_BUFFERED`: Buffers messages to be transmitted later.
 * - `DEBUG_BOTH`: Combines real-time and buffered output.
 */

#include "debug.h"
#include <stdio.h>         // Standard I/O functions
#include <stdarg.h>        // For variadic functions
#include "pico/stdlib.h"   // For `time_us_64()` to obtain timestamps

static char debug_buffer[DEBUG_BUFFER_SIZE]; /**< Buffer for storing debug messages. */
static size_t debug_buffer_index = 0;       /**< Current index in the debug buffer. */
static DebugMode current_debug_mode = DEBUG_NONE; /**< Current debug mode. */

/**
 * @brief Initializes the debug system.
 *
 * Resets the debug buffer and sets the debug mode to `DEBUG_NONE`. This
 * function should be called during system initialization.
 */
void init_debug(void) {
    debug_buffer_index = 0;
    current_debug_mode = DEBUG_NONE;
}

/**
 * @brief Sets the current debug output mode.
 *
 * Changes how debug messages are logged. Modes include real-time console
 * output, buffered logging, or a combination of both.
 *
 * @param mode The desired debug mode. See `DebugMode` for options.
 */
void set_debug_mode(DebugMode mode) {
    current_debug_mode = mode;
}

/**
 * @brief Logs a debug message with optional formatting.
 *
 * Outputs a timestamped message to the console, stores it in the buffer, or both,
 * depending on the current debug mode. Timestamps are displayed in milliseconds
 * since system start and are formatted in bold.
 *
 * @param format The format string, similar to `printf`.
 * @param ... Arguments corresponding to the format string.
 */
    void debug_log(const char* format, ...) {
    va_list args;
    va_start(args, format);

  // Get the current time in microseconds
    uint64_t timestamp_us = time_us_64();
    uint64_t timestamp_ms = timestamp_us / 1000; // Convert to milliseconds

    char timestamp[32];

    // Determine the timestamp format
    if (timestamp_us < 10000) {
        // If total time is less than 10ms, display in microseconds
        snprintf(timestamp, sizeof(timestamp), "\033[1m[%llu us]\033[0m ", (unsigned long long)timestamp_us);
    } else {
        // Otherwise, display whole milliseconds
        snprintf(timestamp, sizeof(timestamp), "\033[1m[%llu ms]\033[0m ", (unsigned long long)timestamp_ms);
    }



    if (current_debug_mode == DEBUG_REALTIME || current_debug_mode == DEBUG_BOTH) {
        // Real-time output to console
        printf("%s", timestamp);  // Print the timestamp
        vprintf(format, args);   // Print the formatted message
    }

    if (current_debug_mode == DEBUG_BUFFERED || current_debug_mode == DEBUG_BOTH) {
        // Store message in the debug buffer
        if (debug_buffer_index < DEBUG_BUFFER_SIZE) {
            int written = snprintf(&debug_buffer[debug_buffer_index],
                                   DEBUG_BUFFER_SIZE - debug_buffer_index,
                                   "%s", timestamp);
            if (written > 0) debug_buffer_index += written;

            written = vsnprintf(&debug_buffer[debug_buffer_index],
                                DEBUG_BUFFER_SIZE - debug_buffer_index,
                                format, args);
            if (written > 0) debug_buffer_index += written;

            // Ensure the buffer doesn't overflow
            if (debug_buffer_index >= DEBUG_BUFFER_SIZE) {
                debug_buffer_index = DEBUG_BUFFER_SIZE - 1;
            }
        }
    }

    va_end(args);
}

/**
 * @brief Logs a debug message with optional color and formatting.
 *
 * Outputs a timestamped message to the console, stores it in the buffer, or both,
 * depending on the current debug mode. Timestamps are formatted in bold, and
 * the message can include ANSI color escape codes for enhanced readability.
 *
 * @param color The ANSI escape code for the desired text color (e.g., `"\033[31m"` for red).
 *              Use an empty string (`""`) for default text color.
 * @param format The format string, similar to `printf`.
 * @param ... Arguments corresponding to the format string.
 */
void debug_log_with_color(const char* color, const char* format, ...) {
    va_list args;
    va_start(args, format);

    // Get the current time in microseconds
    uint64_t timestamp_us = time_us_64();
    uint64_t timestamp_ms = timestamp_us / 1000; // Convert to milliseconds

    char timestamp[32];

    // Determine the timestamp format
    if (timestamp_us < 10000) {
        // If total time is less than 10ms, display in microseconds
        snprintf(timestamp, sizeof(timestamp), "\033[1m[%llu us]\033[0m ", (unsigned long long)timestamp_us);
    } else {
        // Otherwise, display whole milliseconds
        snprintf(timestamp, sizeof(timestamp), "\033[1m[%llu ms]\033[0m ", (unsigned long long)timestamp_ms);
    }

    if (current_debug_mode == DEBUG_REALTIME || current_debug_mode == DEBUG_BOTH) {
        // Real-time output to console with color
        printf("%s", timestamp);   // Print the timestamp
        printf("%s", color);       // Apply color formatting
        vprintf(format, args);    // Print the formatted message
        printf(COLOR_RESET);       // Reset text formatting
    }

    if (current_debug_mode == DEBUG_BUFFERED || current_debug_mode == DEBUG_BOTH) {
        // Store message in the debug buffer
        if (debug_buffer_index < DEBUG_BUFFER_SIZE) {
            int written = snprintf(&debug_buffer[debug_buffer_index],
                                   DEBUG_BUFFER_SIZE - debug_buffer_index,
                                   "%s", timestamp);
            if (written > 0) debug_buffer_index += written;

            written = snprintf(&debug_buffer[debug_buffer_index],
                               DEBUG_BUFFER_SIZE - debug_buffer_index,
                               "%s", color);
            if (written > 0) debug_buffer_index += written;

            written = vsnprintf(&debug_buffer[debug_buffer_index],
                                DEBUG_BUFFER_SIZE - debug_buffer_index,
                                format, args);
            if (written > 0) debug_buffer_index += written;

            // Add reset sequence to the buffer
            written = snprintf(&debug_buffer[debug_buffer_index],
                               DEBUG_BUFFER_SIZE - debug_buffer_index,
                               "%s", COLOR_RESET);
            if (written > 0) debug_buffer_index += written;

            // Prevent buffer overflow
            if (debug_buffer_index >= DEBUG_BUFFER_SIZE) {
                debug_buffer_index = DEBUG_BUFFER_SIZE - 1;
            }
        }
    }

    va_end(args);
}

/**
 * @brief Transmits all buffered debug logs to the console.
 *
 * Outputs all stored debug messages in the buffer to the console. Typically
 * used before system shutdown or for periodic diagnostics. Clears the buffer
 * after transmission.
 */
void transmit_debug_logs(void) {
    if (current_debug_mode == DEBUG_BUFFERED || current_debug_mode == DEBUG_BOTH) {
        printf("Buffered debug log:\n%s", debug_buffer);
        debug_buffer_index = 0; // Clear the buffer
    }
}
