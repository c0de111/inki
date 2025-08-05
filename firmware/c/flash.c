// webserver.c
#include "hardware/sync.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "webserver.h"
#include "lwip/tcp.h"
#include <string.h>
#include <stdio.h>
#include "pico/time.h"
#include "debug.h"
#include "main.h"
#include "flash.h"
#include "wifi.h"
#include "GUI_Paint.h"               // Paint_DrawString_EN
#include "ImageResources.h"          // BlackImage
#include "fonts.h"                   // Fonts, falls nicht via GUI_Paint.h
#include "DEV_Config.h"    // For device configuration
#include "GUI_Paint.h"     // For painting functions (e.g., Paint_NewImage)
#include "ImageResources.h"     // For image-related data
#include "EPD_7in5_V2.h"
#include "EPD_4in2_V2.h"
#include "EPD_2in9_V2.h"
#include "ds3231.h"
#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"

// ------------------------------
// CRC32 implementation
// ------------------------------
static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

static void init_crc32_table(void) {
    if (crc32_table_initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_initialized = true;
}

static uint32_t calc_crc32(const void* data, size_t len) {
    init_crc32_table();
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* buf = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

bool get_firmware_slot_info(
    uint8_t slot,
    char* build_date,
    char* git_version,
    uint32_t* size,
    uint32_t* crc32,
    uint8_t* slot_index,
    uint8_t* valid_flag
) {
    uint32_t offset = (slot == 0) ? FIRMWARE_SLOT0_FLASH_OFFSET :
    (slot == 1) ? FIRMWARE_SLOT1_FLASH_OFFSET : 0;

    if (offset == 0) {
        return false; // Invalid slot
    }

    const firmware_header_t* header = (const firmware_header_t*)FLASH_PTR(offset);

    if (memcmp(header->magic, FIRMWARE_MAGIC, sizeof(header->magic)) != 0 || header->valid_flag != 1) {
        return false;
    }

    if (build_date) {
        strncpy(build_date, header->build_date, sizeof(header->build_date) - 1);
        build_date[sizeof(header->build_date) - 1] = '\0';
    }

    if (git_version) {
        strncpy(git_version, header->git_version, sizeof(header->git_version) - 1);
        git_version[sizeof(header->git_version) - 1] = '\0';
    }

    if (size) {
        *size = header->firmware_size;
    }

    if (crc32) {
        *crc32 = header->crc32;
    }

    if (slot_index) {
        *slot_index = header->slot;
    }

    if (valid_flag) {
        *valid_flag = header->valid_flag;
    }

    return true;
}

bool get_flash_logo_info(int* width, int* height, int* datalen) {
    const logo_header_t* header = (const logo_header_t*)FLASH_PTR(LOGO_FLASH_OFFSET);

    if (memcmp(header->magic, "LOGO", 4) != 0) {
        return false;
    }

    if (width) *width = header->width;
    if (height) *height = header->height;
    if (datalen) *datalen = header->datalen;

    return true;
}

const char* get_active_firmware_slot_info(void) {
    // Read current VTOR (Vector Table Offset Register) address
    uintptr_t vtor = *((volatile uint32_t*)(PPB_BASE + M0PLUS_VTOR_OFFSET));

    // Read reset handler address from vector table (entry 1 = reset handler)
    uintptr_t reset_handler = ((uintptr_t*)vtor)[1];

    const char* slot_name;

    if (reset_handler >= (uintptr_t)FLASH_PTR(BOOTLOADER_FLASH_OFFSET) &&
        reset_handler <  (uintptr_t)FLASH_PTR(FIRMWARE_SLOT0_FLASH_OFFSET)) {
        slot_name = "SLOT_DIRECT";
        } else if (reset_handler >= (uintptr_t)FLASH_PTR(FIRMWARE_SLOT0_FLASH_OFFSET) &&
            reset_handler <  (uintptr_t)FLASH_PTR(FIRMWARE_SLOT0_FLASH_OFFSET + FIRMWARE_FLASH_SIZE)) {
            slot_name = "SLOT_0";
            } else if (reset_handler >= (uintptr_t)FLASH_PTR(FIRMWARE_SLOT1_FLASH_OFFSET) &&
                reset_handler <  (uintptr_t)FLASH_PTR(FIRMWARE_SLOT1_FLASH_OFFSET + FIRMWARE_FLASH_SIZE)) {
                slot_name = "SLOT_1";
                } else {
                    slot_name = "SLOT_UNKNOWN";
                }

                // Static buffer for formatted return string
                static char info[64];
            snprintf(info, sizeof(info), "%s (Reset @ 0x%08lX)", slot_name, (unsigned long)reset_handler);
            return info;
}

/* Default Wi-Fi config written to .wifi_config section */
// __attribute__((section(".wifi_config"), used))
// const wifi_config_t wifi_config_flash = {
//     .ssid = "default_ssid",
//     .password = "default_password",
//     .crc32 = 0  // Calculated at runtime
// };


/* Default Seatsurfing config written to .seatsurfing_config section */
// __attribute__((section(".seatsurfing_config"), used))
// const seatsurfing_config_t seatsurfing_config_flash = {
//     .data = {
//         .host = "seatsurfing.io",
//         .ip   = {192, 168, 178, 85},
//         .port = 8080,
//         .username = "default_esign@seatsurfing.local",
//         .password = "default_password",
//         .space_id = "default_space_id", // seatsurfing space_id, can be found in booking link in "Areas"
//         .location_id = "default_location_id" // seatsurfing location_id, can be found in booking link in "Areas"
//     },
//     .crc32 = 0  // Calculated at runtime
// };

/* Default User config written to .device_config section */
// __attribute__((section(".device_config"), used))
// const device_config_t device_config_flash = {
//     .data = {
//         .roomname = "Room 204",
//         .type = ROOM_TYPE_OFFICE,
//         .epapertype = EPAPER_WAVESHARE_4IN2_V2,
//         .refresh_minutes_by_pushbutton = {30, 30, 30, 30, 30, 30, 30, 30},
//         .show_query_date = true,
//         .query_only_at_officehours = false,
//         .switch_off_battery_voltage = 2.7,
//         .description = "Office Space",
//         .number_of_seats = 1,
//         .number_of_people_meeting = 1,
//         .has_projector = false,
//         .has_conferencesystem = false,
//         .conversion_factor = 0.00169,
//         .wifi_reconnect_minutes = 5,
//         .watchdog_time = 8000,
//         .wifi_timeout = 1000, //4000,
//         .number_wifi_attempts = 6,
//         .max_wait_data_wifi = 100,
//         .pushbutton1_pin = 7,
//         .pushbutton2_pin = 6,
//         .pushbutton3_pin = 5,
//         .num_pushbuttons = 3,
//     },
//     .crc32 = 0  // Calculated at runtime
// };

bool save_uploaded_logo_to_flash(const uint8_t* data, size_t len) {
    if (len < 18) {
        debug_log_with_color(COLOR_RED, "Logo upload failed: data too short (%d bytes)\n", len);
        return false;
    }

    if (memcmp(data, "LOGO", 4) != 0) {
        debug_log_with_color(COLOR_RED, "Logo upload failed: invalid magic header\n");
        return false;
    }

    uint16_t width    = data[4] | (data[5] << 8);
    uint16_t height   = data[6] | (data[7] << 8);
    uint32_t datalen  = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);
    size_t expected   = datalen + 18;

    if (expected != len) {
        debug_log_with_color(COLOR_RED, "Logo upload failed: datalen mismatch (%d + 18 != %d)\n", datalen, len);
        return false;
    }

    if (len > LOGO_FLASH_SIZE) {
        debug_log_with_color(COLOR_RED, "Logo upload failed: file too large (%d > %d bytes)\n", len, LOGO_FLASH_SIZE);
        return false;
    }

    debug_log_with_color(COLOR_GREEN, "Logo upload OK: %dx%d px, %d bytes total\n", width, height, len);

    // optional: round up to 256-byte alignment
    uint8_t padded[LOGO_FLASH_SIZE] = {0};
    memcpy(padded, data, len);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(LOGO_FLASH_OFFSET, LOGO_FLASH_SIZE);
    flash_range_program(LOGO_FLASH_OFFSET, padded, LOGO_FLASH_SIZE);
    restore_interrupts(ints);

    debug_log_with_color(COLOR_YELLOW, "Logo written to Flash at offset 0x%X\n", LOGO_FLASH_OFFSET);
    return true;
}


// ------------------------------
// Wi-Fi config functions
// ------------------------------
bool load_wifi_config(wifi_config_t* out_config) {
    const wifi_config_t* flash_config = (const wifi_config_t*)FLASH_PTR(WIFI_CONFIG_FLASH_OFFSET);
    memcpy(out_config, flash_config, sizeof(wifi_config_t));

    uint32_t expected_crc = calc_crc32(out_config, sizeof(wifi_config_t) - sizeof(uint32_t));
    return (expected_crc == out_config->crc32);
}

bool save_wifi_config(const wifi_config_t* in_config) {
    wifi_config_t temp = *in_config;
    temp.crc32 = calc_crc32(&temp, sizeof(wifi_config_t) - sizeof(uint32_t));

    const uint32_t sector_offset = WIFI_CONFIG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_offset, FLASH_SECTOR_SIZE);
    flash_range_program(WIFI_CONFIG_FLASH_OFFSET, (const uint8_t*)&temp, sizeof(wifi_config_t));
    restore_interrupts(ints);

    return true;
}

void init_wifi_config(wifi_config_t* out_config) {
    if (!load_wifi_config(out_config)) {
        save_wifi_config((const void*)WIFI_CONFIG_FLASH_OFFSET);
        *out_config = wifi_config_flash;
    }
}

// ------------------------------
// Seatsurfing config functions
// ------------------------------
bool load_seatsurfing_config(seatsurfing_config_t* out_config) {
    const seatsurfing_config_t* flash_config = (const seatsurfing_config_t*)FLASH_PTR(SEATSURFING_CONFIG_FLASH_OFFSET);
    memcpy(out_config, flash_config, sizeof(seatsurfing_config_t));

    uint32_t expected_crc = calc_crc32(&out_config->data, sizeof(seatsurfing_config_data_t));
    return (expected_crc == out_config->crc32);
}

bool save_seatsurfing_config(const seatsurfing_config_t* in_config) {
    seatsurfing_config_t temp = *in_config;
    temp.crc32 = calc_crc32(&temp.data, sizeof(seatsurfing_config_data_t));

    const uint32_t sector_offset = SEATSURFING_CONFIG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_offset, FLASH_SECTOR_SIZE);
    flash_range_program(SEATSURFING_CONFIG_FLASH_OFFSET, (const uint8_t*)&temp, sizeof(seatsurfing_config_t));
    restore_interrupts(ints);

    return true;
}

void init_seatsurfing_config(seatsurfing_config_t* out_config) {
    if (!load_seatsurfing_config(out_config)) {
        save_seatsurfing_config(&seatsurfing_config_flash);
        *out_config = seatsurfing_config_flash;
    }
}

// ------------------------------
// Device config functions
// ------------------------------
bool load_device_config(device_config_t* out_config) {
    const device_config_t* flash_config = (const device_config_t*)FLASH_PTR(DEVICE_CONFIG_FLASH_OFFSET);
    memcpy(out_config, flash_config, sizeof(device_config_t));

    uint32_t expected_crc = calc_crc32(&out_config->data, sizeof(device_config_data_t));
    return (expected_crc == out_config->crc32);
}

bool save_device_config(const device_config_t* in_config) {
    device_config_t temp = *in_config;
    temp.crc32 = calc_crc32(&temp.data, sizeof(device_config_data_t));

    const uint32_t sector_offset = DEVICE_CONFIG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_offset, FLASH_SECTOR_SIZE);
    flash_range_program(DEVICE_CONFIG_FLASH_OFFSET, (const uint8_t*)&temp, sizeof(device_config_t));
    restore_interrupts(ints);

    return true;
}

void init_device_config(device_config_t* out_config) {
    if (!load_device_config(out_config)) {
        save_device_config(&device_config_flash);
        *out_config = device_config_flash;
    }
}

const void* keep_device_config_flash = &device_config_flash;
const void* keep_wifi_config_flash = &wifi_config_flash;
const void* keep_seatsurfing_config_flash = &seatsurfing_config_flash;
