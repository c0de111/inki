#pragma once
#include <stdbool.h>
#include "config.h"
#include "seatsurfing_config.h"
#include "device_config.h"
#include "wifi_config.h"

// #include "pico/flash.h"
#include <stddef.h>

#define LOGO_MAGIC "LOGO"

#define FIRMWARE_MAGIC "inki_firmware"
#define FIRMWARE_MAGIC_LEN (sizeof(FIRMWARE_MAGIC) - 1)
_Static_assert(FIRMWARE_MAGIC_LEN == 13, "FIRMWARE_MAGIC_LEN mismatch");

/*
 * RP2040 Flash Memory Map (2 MB = 0x200000)
 * ┌──────────────┬──────────────────────┬────────────┬──────────────────────────────────────┐
 * │   Address    │       Region         │   Size     │              Description             │
 * ├──────────────┼──────────────────────┼────────────┼──────────────────────────────────────┤
 * │ 0x000000     │ Bootloader           │  64 KB     │ Custom bootloader                    │
 * │ 0x010000     │ Firmware Slot 0      │ 940 KB     │ FIRMWARE_FLASH_SIZE = 0xEB800        │
 * │ 0x0FB800     │ Firmware Slot 1      │ 940 KB     │ FIRMWARE_FLASH_SIZE = 0xEB800        │
 * │ 0x1E7000     │ Config & Reserved    │ 100 KB     │ Configuration, logos, OTA buffers    │
 * │ 0x200000     │ Flash End            │            │ End of 2 MB QSPI flash               │
 * └──────────────┴──────────────────────┴────────────┴──────────────────────────────────────┘
 *
 * Firmware slots start at XIP_BASE + offset:
 *   - Slot 0: 0x10000000 + 0x010000 = 0x10010000
 *   - Slot 1: 0x10000000 + 0x0FB800 = 0x100FB800
 *
 * Notes:
 * - Bootloader include 256-byte boot2 at the start of the binary.
 * - Firmware binaries include a 256-byte firmware header at the start of the binary.
 * - "Config & Reserved" includes all flash-persistent settings and data.
 */

#define BOOTLOADER_FLASH_OFFSET           0x000000 // bootloader (size: 64 kb) starts with boot2 (256 byte), directly after that vectortable custom bootloader
#define FIRMWARE_SLOT0_FLASH_OFFSET       0x000000 + 0x010000  // = 0x010000 (size: 940 kb) has to be identical in application1_memmap.ld and flash.sh
#define FIRMWARE_SLOT1_FLASH_OFFSET       0x000000 + 0x010000 + 0xEB800 // = 0x0FB800 (size: 940 kb) has to be identical in application1_memmap.ld and flash.sh
#define CONFIG_FLASH_OFFSET               0x1E7000  // Start of reserved flash region (top 100 KB)
#define VECTOR_TABLE_OFFSET               0x100  // first 256 Byte of respective firmware store firmware_header_t, than vector_table; no boot2

// Configuration blocks (each 4 KB = 1 flash sector)
#define WIFI_CONFIG_FLASH_OFFSET          (CONFIG_FLASH_OFFSET + 0x0000)  // 0x1E7000 - Wi-Fi credentials
#define wifi_config_flash (*(const wifi_config_t*)FLASH_PTR(WIFI_CONFIG_FLASH_OFFSET))
#define SEATSURFING_CONFIG_FLASH_OFFSET   (CONFIG_FLASH_OFFSET + 0x1000)  // 0x1E8000 - Seatsurfing API settings
#define seatsurfing_config_flash (*(const seatsurfing_config_t*)FLASH_PTR(SEATSURFING_CONFIG_FLASH_OFFSET))
#define DEVICE_CONFIG_FLASH_OFFSET        (CONFIG_FLASH_OFFSET + 0x2000)  // 0x1E9000 - Device and UI configuration
#define device_config_flash (*(const device_config_t*)FLASH_PTR(DEVICE_CONFIG_FLASH_OFFSET))

// Logo storage block (8 KB = 2 flash sectors)
#define LOGO_FLASH_OFFSET                 (CONFIG_FLASH_OFFSET + 0x3000)  // 0x1EA000 - Uploadable logo binary
#define LOGO_FLASH_SIZE                   0x2000                          // 8192 bytes reserved for 1-bit bitmap logo, has to be in multiples of FLASH_SECTOR_SIZE

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE                   256  // 0x100 = 256, entspricht (1u << 8)
#endif

#define FIRMWARE_FLASH_SIZE               0xEB800  // 962,560 bytes, 235 flash sectors

// Flash access
#define FLASH_PTR(offset)          ((const uint8_t *)(XIP_BASE + (offset)))
// #define FLASH_SECTOR_SIZE          0x1000  // already defined in pico SDK

static struct {
    uint8_t buffer[16*FLASH_PAGE_SIZE];
    size_t buffer_filled;
    size_t flash_offset;
} flash_writer;

// ┌───────────────────────────────────────────────┐
// │          Typical usage for LOGO_FLASH         │
// └───────────────────────────────────────────────┘
// - Custom user-uploaded logo via web interface
// - Format: raw 1-bit image with 18-byte header
// - Fits up to 200x200 pixels (5000 B) safely

typedef struct __attribute__((packed)) { // packed so that header is not padded for correct use of bitmap flash adressing
    char magic[4];
    uint16_t width;
    uint16_t height;
    uint32_t datalen;
    uint8_t reserved[6];
} logo_header_t;

typedef struct __attribute__((packed)) {
    char magic[13];             // "inki_firmware"
    uint8_t valid_flag;         // 1 = gültig
    char build_date[16];        // z. B. "2025-07-16 13:30"
    char git_version[32];       // "v1.0.6-60-gcddef-dirty"
    uint32_t firmware_size;     // Byte-Anzahl der Firmwaredaten
    uint8_t slot;               // 0 = slot0, 1 = slot1, 255 = direct
    uint32_t crc32;             // CRC über Firmwaredaten
    uint8_t reserved[185];      // Reserve für spätere Erweiterung
} firmware_header_t;

/* Wi-Fi config */
bool load_wifi_config(wifi_config_t* out);
bool save_wifi_config(const wifi_config_t* in);
void init_wifi_config(wifi_config_t* out);

/* Seatsurfing config */
bool load_seatsurfing_config(seatsurfing_config_t* out);
bool save_seatsurfing_config(const seatsurfing_config_t* in);
void init_seatsurfing_config(seatsurfing_config_t* out);

/* Device config */
bool load_device_config(device_config_t* out);
bool save_device_config(const device_config_t* in);
void init_device_config(device_config_t* out);

bool save_uploaded_logo_to_flash(const uint8_t* data, size_t len);
const char* get_active_firmware_slot_info(void);
bool get_firmware_slot_info(
    uint8_t slot,
    char* build_date,
    char* git_version,
    uint32_t* size,
    uint32_t* crc32,
    uint8_t* slot_index,
    uint8_t* valid_flag
);
bool get_flash_logo_info(int* width, int* height, int* datalen);
