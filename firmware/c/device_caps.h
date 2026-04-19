#pragma once

#include <stdbool.h>
#include <stdint.h>

// Runtime hardware capability descriptor.
// Built once at boot from I2C probe + strap + epaper init.
// Passed to use_case.render() and used by epaper_render helpers.
typedef struct {
    // ePaper display (resolved from flash config by epaper_init)
    uint16_t epaper_width;
    uint16_t epaper_height;
    bool epaper_4gray; // true when display is in 4-grayscale mode

    // I2C-detected peripherals
    bool st25_present;   // NFC tag (ST25DV)
    bool bmp581_present; // pressure/temperature sensor
    bool ds3231_present; // RTC (primary)
    bool rv3028_present; // RTC (alternate)

    // Hardware strap decoded
    bool coin_cell_present; // false for strap 0x05 (L2 7.5" minimal)
} device_caps_t;
