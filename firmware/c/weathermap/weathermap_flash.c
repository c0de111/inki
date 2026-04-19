#include "weathermap/weathermap_flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#define LOG_MODULE LOG_MOD_WEATHERMAP
#include "debug.h"
#include <string.h>

// =============================================================================
// Weathermap config
// =============================================================================

static const weathermap_config_t s_weathermap_defaults = {
    .data =
        {
            .center_lat = WEATHERMAP_DEFAULT_CENTER_LAT,
            .center_lon = WEATHERMAP_DEFAULT_CENTER_LON,
            .half_width_m = WEATHERMAP_DEFAULT_HALF_WIDTH_M,
            .flags = WEATHERMAP_CONFIG_VERSION,
            .reserved = {0},
        },
    .crc32 = 0,
};

bool load_weathermap_config(weathermap_config_t *out) {
    const weathermap_config_t *p = (const weathermap_config_t *)FLASH_PTR(UC_CONFIG_FLASH_OFFSET);
    memcpy(out, p, sizeof(weathermap_config_t));
    if (calc_crc32(&out->data, sizeof(weathermap_config_data_t)) != out->crc32) {
        dlog("[WMAP CONFIG] CRC mismatch\n");
        return false;
    }
    dlog("[WMAP CONFIG] Loaded lat=%.6f lon=%.6f half=%.2f\n", out->data.center_lat,
         out->data.center_lon, out->data.half_width_m);
    return true;
}

bool save_weathermap_config(const weathermap_config_t *in) {
    weathermap_config_t temp = *in;
    temp.crc32 = calc_crc32(&temp.data, sizeof(weathermap_config_data_t));

    const uint32_t sector = UC_CONFIG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector, FLASH_SECTOR_SIZE);
    flash_range_program(UC_CONFIG_FLASH_OFFSET, (const uint8_t *)&temp,
                        sizeof(weathermap_config_t));
    restore_interrupts(ints);
    return true;
}

void init_weathermap_config(weathermap_config_t *out) {
    if (!load_weathermap_config(out) || out->data.flags != WEATHERMAP_CONFIG_VERSION) {
        dlog("[WMAP CONFIG] CRC fail or version mismatch — resetting to defaults\n");
        weathermap_config_t tmp = s_weathermap_defaults;
        save_weathermap_config(&tmp);
        *out = tmp;
    }
}

// Linker retention — prevents the XIP-mapped default from being stripped.
const void *keep_weathermap_config_flash = &weathermap_config_flash;

// =============================================================================
// Weathermap meta
// =============================================================================

bool get_weathermap_meta(uint32_t *bytes_out) {
    if (bytes_out)
        *bytes_out = 0;
    const weathermap_meta_t *meta =
        (const weathermap_meta_t *)FLASH_PTR(WEATHERMAP_META_FLASH_OFFSET);
    if (memcmp(meta->magic, "WMAP", 4) == 0) {
        if (bytes_out)
            *bytes_out = meta->bytes_count;
        return true;
    }
    return false;
}

bool set_weathermap_meta(uint32_t bytes) {
    weathermap_meta_t meta = {0};
    memcpy(meta.magic, "WMAP", 4);
    meta.bytes_count = bytes;

    uint32_t sector_offset = WEATHERMAP_META_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_offset, FLASH_SECTOR_SIZE);
    flash_range_program(WEATHERMAP_META_FLASH_OFFSET, (const uint8_t *)&meta, sizeof(meta));
    restore_interrupts(ints);
    return true;
}

bool clear_weathermap_meta(void) {
    uint32_t sector_offset = WEATHERMAP_META_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_offset, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    return true;
}

bool weathermap_flash_info(uint16_t *width, uint16_t *height, uint32_t *datalen) {
    const weathermap_image_header_t *hdr =
        (const weathermap_image_header_t *)FLASH_PTR(WEATHERMAP_IMG_FLASH_OFFSET);
    if (memcmp(hdr->magic, "WIMG", 4) != 0)
        return false;
    if (width)
        *width = hdr->width;
    if (height)
        *height = hdr->height;
    if (datalen)
        *datalen = hdr->datalen;
    return true;
}

bool weathermap_flash_clear_image(void) {
    uint32_t start = WEATHERMAP_IMG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(start, WEATHERMAP_IMG_FLASH_SIZE);
    restore_interrupts(ints);
    return true;
}

// =============================================================================
// Weathermap image region streaming writer
// =============================================================================

typedef struct {
    bool active;
    uint32_t write_offset;
    uint32_t total_needed;
    uint32_t written;
    uint8_t page_buf[FLASH_PAGE_SIZE];
    size_t page_filled;
} wmap_img_writer_t;

static wmap_img_writer_t s_wmap_img = {0};

bool weathermap_flash_begin_image(uint16_t width, uint16_t height) {
    if (s_wmap_img.active)
        return false;
    weathermap_image_header_t hdr = {0};
    memcpy(hdr.magic, "WIMG", 4);
    hdr.width = width;
    hdr.height = height;
    hdr.bpp = 2;
    hdr.datalen = ((uint32_t)width * height + 3) / 4;
    hdr.crc32 = 0;

    uint32_t start = WEATHERMAP_IMG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t erase_len =
        ((sizeof(hdr) + hdr.datalen + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1));

    uint8_t page_hdr[FLASH_PAGE_SIZE];
    memset(page_hdr, 0xFF, sizeof(page_hdr));
    memcpy(page_hdr, &hdr, sizeof(hdr));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(start, erase_len);
    flash_range_program(WEATHERMAP_IMG_FLASH_OFFSET, page_hdr, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    s_wmap_img.active = true;
    s_wmap_img.write_offset = WEATHERMAP_IMG_FLASH_OFFSET + FLASH_PAGE_SIZE;
    s_wmap_img.total_needed = hdr.datalen;
    s_wmap_img.written = 0;
    s_wmap_img.page_filled = 0;
    return true;
}

bool weathermap_flash_append_row_2bpp(const uint8_t *row_packed, size_t row_len) {
    if (!s_wmap_img.active || !row_packed || row_len == 0)
        return false;
    if (s_wmap_img.written + row_len > s_wmap_img.total_needed)
        row_len = s_wmap_img.total_needed - s_wmap_img.written;
    size_t off = 0;
    while (off < row_len) {
        size_t space = FLASH_PAGE_SIZE - s_wmap_img.page_filled;
        size_t n = (row_len - off < space) ? (row_len - off) : space;
        memcpy(s_wmap_img.page_buf + s_wmap_img.page_filled, row_packed + off, n);
        s_wmap_img.page_filled += n;
        off += n;
        if (s_wmap_img.page_filled == FLASH_PAGE_SIZE) {
            uint32_t ints = save_and_disable_interrupts();
            flash_range_program(s_wmap_img.write_offset, s_wmap_img.page_buf, FLASH_PAGE_SIZE);
            restore_interrupts(ints);
            s_wmap_img.write_offset += FLASH_PAGE_SIZE;
            s_wmap_img.written += FLASH_PAGE_SIZE;
            s_wmap_img.page_filled = 0;
        }
    }
    return true;
}

bool weathermap_flash_end_image(void) {
    if (!s_wmap_img.active)
        return false;
    if (s_wmap_img.page_filled > 0) {
        memset(s_wmap_img.page_buf + s_wmap_img.page_filled, 0xFF,
               FLASH_PAGE_SIZE - s_wmap_img.page_filled);
        uint32_t ints = save_and_disable_interrupts();
        flash_range_program(s_wmap_img.write_offset, s_wmap_img.page_buf, FLASH_PAGE_SIZE);
        restore_interrupts(ints);
        s_wmap_img.write_offset += FLASH_PAGE_SIZE;
        s_wmap_img.written += s_wmap_img.page_filled;
        s_wmap_img.page_filled = 0;
    }
    s_wmap_img.active = false;
    return (s_wmap_img.written >= s_wmap_img.total_needed);
}

// =============================================================================
// PNG staging writer into slot1
// =============================================================================

typedef struct {
    bool active;
    uint32_t write_offset;
    uint32_t total_bytes;
    uint32_t last_erased_sector;
    uint8_t page_buf[FLASH_PAGE_SIZE];
    size_t page_filled;
} wmap_stage_t;

static wmap_stage_t s_wmap_stage = {0};

static inline void ensure_sector_erased(uint32_t abs_offset) {
    uint32_t sector_base = abs_offset & ~(FLASH_SECTOR_SIZE - 1);
    if (s_wmap_stage.last_erased_sector != sector_base) {
        flash_range_erase(sector_base, FLASH_SECTOR_SIZE);
        s_wmap_stage.last_erased_sector = sector_base;
    }
}

bool wmap_staging_begin(void) {
    s_wmap_stage.active = true;
    s_wmap_stage.write_offset = FIRMWARE_SLOT1_FLASH_OFFSET;
    s_wmap_stage.total_bytes = 0;
    s_wmap_stage.last_erased_sector = 0xFFFFFFFFu;
    s_wmap_stage.page_filled = 0;
    return true;
}

bool wmap_staging_append(const uint8_t *data, size_t len) {
    if (!s_wmap_stage.active || !data || len == 0)
        return true; // treat as no-op
    while (len > 0) {
        size_t space = FLASH_PAGE_SIZE - s_wmap_stage.page_filled;
        size_t n = (len < space) ? len : space;
        memcpy(s_wmap_stage.page_buf + s_wmap_stage.page_filled, data, n);
        s_wmap_stage.page_filled += n;
        data += n;
        len -= n;
        if (s_wmap_stage.page_filled == FLASH_PAGE_SIZE) {
            uint32_t ints = save_and_disable_interrupts();
            ensure_sector_erased(s_wmap_stage.write_offset);
            flash_range_program(s_wmap_stage.write_offset, s_wmap_stage.page_buf, FLASH_PAGE_SIZE);
            restore_interrupts(ints);
            s_wmap_stage.write_offset += FLASH_PAGE_SIZE;
            s_wmap_stage.total_bytes += FLASH_PAGE_SIZE;
            s_wmap_stage.page_filled = 0;
        }
    }
    return true;
}

bool wmap_staging_end(uint32_t *total_bytes) {
    if (!s_wmap_stage.active)
        return false;
    if (s_wmap_stage.page_filled > 0) {
        memset(s_wmap_stage.page_buf + s_wmap_stage.page_filled, 0xFF,
               FLASH_PAGE_SIZE - s_wmap_stage.page_filled);
        uint32_t ints = save_and_disable_interrupts();
        ensure_sector_erased(s_wmap_stage.write_offset);
        flash_range_program(s_wmap_stage.write_offset, s_wmap_stage.page_buf, FLASH_PAGE_SIZE);
        restore_interrupts(ints);
        s_wmap_stage.write_offset += FLASH_PAGE_SIZE;
        s_wmap_stage.total_bytes += s_wmap_stage.page_filled;
        s_wmap_stage.page_filled = 0;
    }
    if (total_bytes)
        *total_bytes = s_wmap_stage.total_bytes;
    s_wmap_stage.active = false;
    return true;
}

void wmap_staging_abort(void) {
    s_wmap_stage.active = false;
    s_wmap_stage.page_filled = 0;
}

const uint8_t *wmap_staging_ptr(void) { return FLASH_PTR(FIRMWARE_SLOT1_FLASH_OFFSET); }

uint32_t wmap_staging_size(void) { return s_wmap_stage.total_bytes; }
