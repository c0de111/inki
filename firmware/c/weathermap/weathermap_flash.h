#pragma once
#include "flash.h"
#include "weathermap/config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Config access
#define weathermap_config_flash (*(const weathermap_config_t *)FLASH_PTR(UC_CONFIG_FLASH_OFFSET))

bool load_weathermap_config(weathermap_config_t *out);
bool save_weathermap_config(const weathermap_config_t *in);
void init_weathermap_config(weathermap_config_t *out);

// Weathermap meta (marker sector)
#define WEATHERMAP_META_FLASH_OFFSET (CONFIG_FLASH_OFFSET + 0x5000) // 0x1EC000

typedef struct __attribute__((packed)) {
    char magic[4]; // "WMAP"
    uint32_t bytes_count;
    uint8_t reserved[8];
} weathermap_meta_t;

bool get_weathermap_meta(uint32_t *bytes_out);
bool set_weathermap_meta(uint32_t bytes);
bool clear_weathermap_meta(void);

// Weathermap 2-bpp image storage
#define WEATHERMAP_IMG_FLASH_OFFSET (CONFIG_FLASH_OFFSET + 0x6000) // 0x1ED000
#define WEATHERMAP_IMG_FLASH_SIZE 0x10000                          // 64 KB

typedef struct __attribute__((packed)) {
    char magic[4]; // "WIMG"
    uint16_t width;
    uint16_t height;
    uint8_t bpp; // bits per pixel in payload (2)
    uint8_t reserved[3];
    uint32_t datalen; // bytes of packed payload following header
    uint32_t crc32;   // optional CRC of payload (0 if unused)
} weathermap_image_header_t;

bool weathermap_flash_info(uint16_t *width, uint16_t *height, uint32_t *datalen);
bool weathermap_flash_clear_image(void);

// Streaming writer into the weathermap image region
bool weathermap_flash_begin_image(uint16_t width, uint16_t height);
bool weathermap_flash_append_row_2bpp(const uint8_t *row_packed, size_t row_len);
bool weathermap_flash_end_image(void);

// PNG staging into slot1 (streaming, tolerates firmware OTA overwrite)
bool wmap_staging_begin(void);
bool wmap_staging_append(const uint8_t *data, size_t len);
bool wmap_staging_end(uint32_t *total_bytes);
void wmap_staging_abort(void);
const uint8_t *wmap_staging_ptr(void);
uint32_t wmap_staging_size(void);
