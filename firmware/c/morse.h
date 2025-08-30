#pragma once
#include <stdbool.h>

// Initialize Morse engine with defaults.
void morse_init(void);

// Enable or disable Morse ticking.
void morse_set_enabled(bool enabled);

// Set Morse unit duration in milliseconds.
void morse_set_unit_ms(int unit_ms);

// Set the message to be morsed; converts to uppercase and supports A-Z, 0-9, space.
// Unsupported characters are treated as spaces (word gaps).
void morse_set_message(const char* text);

// Non-blocking tick; call regularly in the setup loop.
void morse_tick(void);

