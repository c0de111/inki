#pragma once
#include <stdbool.h>
#include <stdint.h>

// Global enable/disable (persisted via flash config, overridden in setup mode)
void led_set_enabled(bool enabled);

// External LED on a GPIO (e.g., GP16 near front LED)
void ext_led_init(uint8_t gpio);
void ext_led_on(void);
void ext_led_off(void);
void ext_led_toggle(void);
bool ext_led_is_initialized(void);

// Pico W onboard LED (via CYW43). Requires cyw43_arch_init() in the caller.
void board_led_on(void);
void board_led_off(void);
void board_led_toggle(void);
