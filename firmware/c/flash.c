// webserver.c
#include "pico/flash.h"
#include "DEV_Config.h" // For device configuration
#include "EPD_2in9_V2.h"
#include "EPD_4in2_V2.h"
#include "EPD_7in5_V2.h"
#include "GUI_Paint.h"      // Paint_DrawString_EN
#include "GUI_Paint.h"      // For painting functions (e.g., Paint_NewImage)
#include "ImageResources.h" // BlackImage
#include "ImageResources.h" // For image-related data
#include "debug.h"
#include "ds3231.h"
#include "flash.h"
#include "fonts.h" // Fonts, falls nicht via GUI_Paint.h
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "lwip/tcp.h"
#include "main.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "webserver.h"
#include "wifi.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef USE_CASE_WEATHERMAP
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
#endif

// ------------------------------
// CRC32 implementation
// ------------------------------
static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

static void init_crc32_table(void) {
    if (crc32_table_initialized)
        return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_initialized = true;
}

static uint32_t calc_crc32(const void *data, size_t len) {
    init_crc32_table();
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *buf = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

bool get_firmware_slot_info(uint8_t slot, char *build_date, char *git_version, uint32_t *size,
                            uint32_t *crc32, uint8_t *slot_index, uint8_t *valid_flag) {
    uint32_t offset = (slot == 0)   ? FIRMWARE_SLOT0_FLASH_OFFSET
                      : (slot == 1) ? FIRMWARE_SLOT1_FLASH_OFFSET
                                    : 0;

    if (offset == 0) {
        return false; // Invalid slot
    }

    const firmware_header_t *header = (const firmware_header_t *)FLASH_PTR(offset);

    if (memcmp(header->magic, FIRMWARE_MAGIC, sizeof(header->magic)) != 0 ||
        header->valid_flag != 1) {
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

bool get_flash_logo_info(int *width, int *height, int *datalen) {
    const logo_header_t *header = (const logo_header_t *)FLASH_PTR(LOGO_FLASH_OFFSET);

    if (memcmp(header->magic, "LOGO", 4) != 0) {
        return false;
    }

    if (width)
        *width = header->width;
    if (height)
        *height = header->height;
    if (datalen) {
        if (header->datalen > (uint32_t)INT_MAX) {
            return false;
        }
        *datalen = (int)header->datalen;
    }

    return true;
}

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

bool weathermap_flash_write_image_2bpp(const uint8_t *packed_data, size_t packed_len,
                                       uint16_t width, uint16_t height) {
    if (!packed_data || packed_len == 0)
        return false;
    if ((WEATHERMAP_IMG_FLASH_OFFSET + sizeof(weathermap_image_header_t) + packed_len) >
        (CONFIG_FLASH_OFFSET + 0x19000)) {
        debug_log_with_color(COLOR_RED, "[WMAP] Image too large for reserved flash\n");
        return false;
    }

    weathermap_image_header_t hdr = {0};
    memcpy(hdr.magic, "WIMG", 4);
    hdr.width = width;
    hdr.height = height;
    hdr.bpp = 2;
    hdr.datalen = packed_len;
    hdr.crc32 = 0; // optional

    uint32_t sector_offset = WEATHERMAP_IMG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);
    size_t erase_len =
        ((sizeof(hdr) + packed_len + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1));

    // Program header as a full 256-byte page for alignment
    uint8_t page_hdr[FLASH_PAGE_SIZE];
    memset(page_hdr, 0xFF, sizeof(page_hdr));
    memcpy(page_hdr, &hdr, sizeof(hdr));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_offset, erase_len);
    flash_range_program(WEATHERMAP_IMG_FLASH_OFFSET, page_hdr, FLASH_PAGE_SIZE);
    // Program payload starting at next page boundary
    uint32_t data_offset = WEATHERMAP_IMG_FLASH_OFFSET + FLASH_PAGE_SIZE;
    size_t written = 0;
    while (written < packed_len) {
        size_t chunk = packed_len - written;
        if (chunk > FLASH_PAGE_SIZE)
            chunk = FLASH_PAGE_SIZE;
        uint8_t page_buf[FLASH_PAGE_SIZE];
        memset(page_buf, 0xFF, sizeof(page_buf));
        memcpy(page_buf, packed_data + written, chunk);
        flash_range_program(data_offset, page_buf, FLASH_PAGE_SIZE);
        data_offset += FLASH_PAGE_SIZE;
        written += chunk;
    }
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

const uint8_t *weathermap_flash_data_ptr(void) {
    // Payload begins at next page boundary after the 256-byte header page.
    return FLASH_PTR(WEATHERMAP_IMG_FLASH_OFFSET + FLASH_PAGE_SIZE);
}

bool weathermap_flash_clear_image(void) {
    uint32_t start = WEATHERMAP_IMG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t erase_len = WEATHERMAP_IMG_FLASH_SIZE; // reserved size
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(start, erase_len);
    restore_interrupts(ints);
    return true;
}

// --- Slot1 image storage (header page + payload pages) ---
static struct {
    bool active;
    uint32_t write_offset;
    uint32_t total_needed;
    uint32_t written;
    uint8_t page_buf[FLASH_PAGE_SIZE];
    size_t page_filled;
} s_wmap_slot1 = {0};

bool wmap_slot1_begin_image(uint16_t width, uint16_t height) {
    if (s_wmap_slot1.active)
        return false;
    weathermap_image_header_t hdr = {0};
    memcpy(hdr.magic, "WIMG", 4);
    hdr.width = width;
    hdr.height = height;
    hdr.bpp = 2;
    hdr.datalen = ((uint32_t)width * height + 3) / 4;
    hdr.crc32 = 0;

    uint32_t base = FIRMWARE_SLOT1_FLASH_OFFSET;
    uint32_t erase_len =
        ((FLASH_PAGE_SIZE + hdr.datalen + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1));

    uint8_t page_hdr[FLASH_PAGE_SIZE];
    memset(page_hdr, 0xFF, sizeof(page_hdr));
    memcpy(page_hdr, &hdr, sizeof(hdr));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(base & ~(FLASH_SECTOR_SIZE - 1), erase_len);
    flash_range_program(base, page_hdr, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    s_wmap_slot1.active = true;
    s_wmap_slot1.write_offset = base + FLASH_PAGE_SIZE;
    s_wmap_slot1.total_needed = hdr.datalen;
    s_wmap_slot1.written = 0;
    s_wmap_slot1.page_filled = 0;
    return true;
}

bool wmap_slot1_append_row_2bpp(const uint8_t *row_packed, size_t row_len) {
    if (!s_wmap_slot1.active || !row_packed || row_len == 0)
        return false;
    if (s_wmap_slot1.written + row_len > s_wmap_slot1.total_needed) {
        row_len = s_wmap_slot1.total_needed - s_wmap_slot1.written;
    }
    size_t off = 0;
    while (off < row_len) {
        size_t space = FLASH_PAGE_SIZE - s_wmap_slot1.page_filled;
        size_t n = (row_len - off < space) ? (row_len - off) : space;
        memcpy(s_wmap_slot1.page_buf + s_wmap_slot1.page_filled, row_packed + off, n);
        s_wmap_slot1.page_filled += n;
        off += n;
        if (s_wmap_slot1.page_filled == FLASH_PAGE_SIZE) {
            uint32_t ints = save_and_disable_interrupts();
            flash_range_program(s_wmap_slot1.write_offset, s_wmap_slot1.page_buf, FLASH_PAGE_SIZE);
            restore_interrupts(ints);
            s_wmap_slot1.write_offset += FLASH_PAGE_SIZE;
            s_wmap_slot1.written += FLASH_PAGE_SIZE;
            s_wmap_slot1.page_filled = 0;
        }
    }
    return true;
}

bool wmap_slot1_end_image(void) {
    if (!s_wmap_slot1.active)
        return false;
    if (s_wmap_slot1.page_filled > 0) {
        memset(s_wmap_slot1.page_buf + s_wmap_slot1.page_filled, 0xFF,
               FLASH_PAGE_SIZE - s_wmap_slot1.page_filled);
        uint32_t ints = save_and_disable_interrupts();
        flash_range_program(s_wmap_slot1.write_offset, s_wmap_slot1.page_buf, FLASH_PAGE_SIZE);
        restore_interrupts(ints);
        s_wmap_slot1.write_offset += FLASH_PAGE_SIZE;
        s_wmap_slot1.written += s_wmap_slot1.page_filled;
        s_wmap_slot1.page_filled = 0;
    }
    s_wmap_slot1.active = false;
    return (s_wmap_slot1.written >= s_wmap_slot1.total_needed);
}

bool wmap_slot1_image_info(uint16_t *width, uint16_t *height, uint32_t *datalen) {
    const weathermap_image_header_t *hdr =
        (const weathermap_image_header_t *)FLASH_PTR(FIRMWARE_SLOT1_FLASH_OFFSET);
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

const uint8_t *wmap_slot1_image_data_ptr(void) {
    return FLASH_PTR(FIRMWARE_SLOT1_FLASH_OFFSET + FLASH_PAGE_SIZE);
}

// --- Streaming writer for WEATHERMAP image ---
typedef struct {
    bool active;
    uint32_t write_offset; // absolute flash offset for next program
    uint32_t total_needed; // expected payload bytes (rows * row_len)
    uint32_t written;      // how many payload bytes written so far
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

    // Erase enough sectors to hold header + payload
    uint32_t start = WEATHERMAP_IMG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t erase_len =
        ((sizeof(hdr) + hdr.datalen + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1));
    // Program header as full 256-byte page
    uint8_t page_hdr[FLASH_PAGE_SIZE];
    memset(page_hdr, 0xFF, sizeof(page_hdr));
    memcpy(page_hdr, &hdr, sizeof(hdr));
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(start, erase_len);
    flash_range_program(WEATHERMAP_IMG_FLASH_OFFSET, page_hdr, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    s_wmap_img.active = true;
    // Start writing payload at next page boundary after header page
    s_wmap_img.write_offset = WEATHERMAP_IMG_FLASH_OFFSET + FLASH_PAGE_SIZE;
    s_wmap_img.total_needed = hdr.datalen;
    s_wmap_img.written = 0;
    s_wmap_img.page_filled = 0;
    return true;
}

bool weathermap_flash_append_row_2bpp(const uint8_t *row_packed, size_t row_len) {
    if (!s_wmap_img.active || !row_packed || row_len == 0)
        return false;
    // Ensure we do not exceed expected length
    if (s_wmap_img.written + row_len > s_wmap_img.total_needed) {
        row_len = s_wmap_img.total_needed - s_wmap_img.written;
    }
    size_t off = 0;
    while (off < row_len) {
        size_t space = FLASH_PAGE_SIZE - s_wmap_img.page_filled;
        size_t n = (row_len - off < space) ? (row_len - off) : space;
        memcpy(s_wmap_img.page_buf + s_wmap_img.page_filled, row_packed + off, n);
        s_wmap_img.page_filled += n;
        off += n;
        if (s_wmap_img.page_filled == FLASH_PAGE_SIZE) {
            uint32_t ints = save_and_disable_interrupts();
            // Sector erase was done upfront across region
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
        // Pad remaining page with 0xFF and program
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

#ifdef USE_CASE_WEATHERMAP
// --- Streaming staging writer into slot1 (PNG) ---
typedef struct {
    bool active;
    uint32_t write_offset;             // absolute flash offset for next program
    uint32_t total_bytes;              // exact bytes of payload (not rounded to page)
    uint32_t last_erased_sector;       // last sector base we erased
    uint8_t page_buf[FLASH_PAGE_SIZE]; // staging for partial page
    size_t page_filled;                // bytes filled in page_buf
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
        // pad remainder with 0xFF (erased state) before programming
        memset(s_wmap_stage.page_buf + s_wmap_stage.page_filled, 0xFF,
               FLASH_PAGE_SIZE - s_wmap_stage.page_filled);
        uint32_t ints = save_and_disable_interrupts();
        ensure_sector_erased(s_wmap_stage.write_offset);
        flash_range_program(s_wmap_stage.write_offset, s_wmap_stage.page_buf, FLASH_PAGE_SIZE);
        restore_interrupts(ints);
        s_wmap_stage.write_offset += FLASH_PAGE_SIZE;
        s_wmap_stage.total_bytes += s_wmap_stage.page_filled; // only count actual data
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
#endif // USE_CASE_WEATHERMAP

const char *get_active_firmware_slot_info(void) {
    // Read current VTOR (Vector Table Offset Register) address
    uintptr_t vtor = *((volatile uint32_t *)(PPB_BASE + M0PLUS_VTOR_OFFSET));

    // Read reset handler address from vector table (entry 1 = reset handler)
    uintptr_t reset_handler = ((uintptr_t *)vtor)[1];

    const char *slot_name;

    if (reset_handler >= (uintptr_t)FLASH_PTR(BOOTLOADER_FLASH_OFFSET) &&
        reset_handler < (uintptr_t)FLASH_PTR(FIRMWARE_SLOT0_FLASH_OFFSET)) {
        slot_name = "SLOT_DIRECT";
    } else if (reset_handler >= (uintptr_t)FLASH_PTR(FIRMWARE_SLOT0_FLASH_OFFSET) &&
               reset_handler <
                   (uintptr_t)FLASH_PTR(FIRMWARE_SLOT0_FLASH_OFFSET + FIRMWARE_FLASH_SIZE)) {
        slot_name = "SLOT_0";
    } else if (reset_handler >= (uintptr_t)FLASH_PTR(FIRMWARE_SLOT1_FLASH_OFFSET) &&
               reset_handler <
                   (uintptr_t)FLASH_PTR(FIRMWARE_SLOT1_FLASH_OFFSET + FIRMWARE_FLASH_SIZE)) {
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
//         .space_id = "default_space_id", // seatsurfing space_id, can be found in booking link in
//         "Areas" .location_id = "default_location_id" // seatsurfing location_id, can be found in
//         booking link in "Areas"
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

bool save_uploaded_logo_to_flash(const uint8_t *data, size_t len) {
    if (len < 18) {
        debug_log_with_color(COLOR_RED, "Logo upload failed: data too short (%d bytes)\n", len);
        return false;
    }

    if (memcmp(data, "LOGO", 4) != 0) {
        debug_log_with_color(COLOR_RED, "Logo upload failed: invalid magic header\n");
        return false;
    }

    uint16_t width = data[4] | (data[5] << 8);
    uint16_t height = data[6] | (data[7] << 8);
    uint32_t datalen = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);
    size_t expected = datalen + 18;

    if (expected != len) {
        debug_log_with_color(COLOR_RED, "Logo upload failed: datalen mismatch (%d + 18 != %d)\n",
                             datalen, len);
        return false;
    }

    if (len > LOGO_FLASH_SIZE) {
        debug_log_with_color(COLOR_RED, "Logo upload failed: file too large (%d > %d bytes)\n", len,
                             LOGO_FLASH_SIZE);
        return false;
    }

    debug_log_with_color(COLOR_GREEN, "Logo upload OK: %dx%d px, %d bytes total\n", width, height,
                         len);

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
static bool load_wifi_config(wifi_config_t *out_config) {
    const wifi_config_t *flash_config = (const wifi_config_t *)FLASH_PTR(WIFI_CONFIG_FLASH_OFFSET);
    memcpy(out_config, flash_config, sizeof(wifi_config_t));

    uint32_t expected_crc = calc_crc32(out_config, sizeof(wifi_config_t) - sizeof(uint32_t));
    return (expected_crc == out_config->crc32);
}

bool save_wifi_config(const wifi_config_t *in_config) {
    wifi_config_t temp = *in_config;
    temp.crc32 = calc_crc32(&temp, sizeof(wifi_config_t) - sizeof(uint32_t));

    const uint32_t sector_offset = WIFI_CONFIG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_offset, FLASH_SECTOR_SIZE);
    flash_range_program(WIFI_CONFIG_FLASH_OFFSET, (const uint8_t *)&temp, sizeof(wifi_config_t));
    restore_interrupts(ints);

    return true;
}

void init_wifi_config(wifi_config_t *out_config) {
    if (!load_wifi_config(out_config)) {
        save_wifi_config((const void *)WIFI_CONFIG_FLASH_OFFSET);
        *out_config = wifi_config_flash;
    }
}

// ------------------------------
// Seatsurfing config functions
#ifdef USE_CASE_SEATSURFING
// ------------------------------
static bool load_seatsurfing_config(seatsurfing_config_t *out_config) {
    const seatsurfing_config_t *flash_config =
        (const seatsurfing_config_t *)FLASH_PTR(SEATSURFING_CONFIG_FLASH_OFFSET);
    memcpy(out_config, flash_config, sizeof(seatsurfing_config_t));

    uint32_t expected_crc = calc_crc32(&out_config->data, sizeof(seatsurfing_config_data_t));
    return (expected_crc == out_config->crc32);
}

bool save_seatsurfing_config(const seatsurfing_config_t *in_config) {
    seatsurfing_config_t temp = *in_config;
    temp.crc32 = calc_crc32(&temp.data, sizeof(seatsurfing_config_data_t));

    const uint32_t sector_offset = SEATSURFING_CONFIG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_offset, FLASH_SECTOR_SIZE);
    flash_range_program(SEATSURFING_CONFIG_FLASH_OFFSET, (const uint8_t *)&temp,
                        sizeof(seatsurfing_config_t));
    restore_interrupts(ints);

    return true;
}

void init_seatsurfing_config(seatsurfing_config_t *out_config) {
    if (!load_seatsurfing_config(out_config)) {
        save_seatsurfing_config(&seatsurfing_config_flash);
        *out_config = seatsurfing_config_flash;
    }
}
#elif defined(USE_CASE_HISTORIAN)
// ------------------------------
static bool load_historian_config(historian_config_t *out_config) {
    const historian_config_t *flash_config =
        (const historian_config_t *)FLASH_PTR(HISTORIAN_CONFIG_FLASH_OFFSET);
    memcpy(out_config, flash_config, sizeof(historian_config_t));

    uint32_t expected_crc = calc_crc32(&out_config->data, sizeof(historian_config_data_t));
    return (expected_crc == out_config->crc32);
}

bool save_historian_config(const historian_config_t *in_config) {
    historian_config_t temp = *in_config;
    temp.crc32 = calc_crc32(&temp.data, sizeof(historian_config_data_t));

    const uint32_t sector_offset = HISTORIAN_CONFIG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_offset, FLASH_SECTOR_SIZE);
    flash_range_program(HISTORIAN_CONFIG_FLASH_OFFSET, (const uint8_t *)&temp,
                        sizeof(historian_config_t));
    restore_interrupts(ints);

    return true;
}

void init_historian_config(historian_config_t *out_config) {
    if (!load_historian_config(out_config)) {
        save_historian_config(&historian_config_flash);
        *out_config = historian_config_flash;
    }
}
#elif defined(USE_CASE_HOMEMATIC)
// ------------------------------
static bool load_homematic_config(homematic_config_t *out_config) {
    const homematic_config_t *flash_config =
        (const homematic_config_t *)FLASH_PTR(HOMEMATIC_CONFIG_FLASH_OFFSET);
    memcpy(out_config, flash_config, sizeof(homematic_config_t));

    uint32_t expected_crc = calc_crc32(&out_config->data, sizeof(homematic_config_data_t));
    return (expected_crc == out_config->crc32);
}

bool save_homematic_config(const homematic_config_t *in_config) {
    homematic_config_t temp = *in_config;
    temp.crc32 = calc_crc32(&temp.data, sizeof(homematic_config_data_t));

    const uint32_t sector_offset = HOMEMATIC_CONFIG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_offset, FLASH_SECTOR_SIZE);
    flash_range_program(HOMEMATIC_CONFIG_FLASH_OFFSET, (const uint8_t *)&temp,
                        sizeof(homematic_config_t));
    restore_interrupts(ints);

    return true;
}

void init_homematic_config(homematic_config_t *out_config) {
    if (!load_homematic_config(out_config)) {
        save_homematic_config(&homematic_config_flash);
        *out_config = homematic_config_flash;
    }
}
#elif defined(USE_CASE_WEATHERMAP)
// ------------------------------
bool load_weathermap_config(weathermap_config_t *out_config) {
    const weathermap_config_t *flash_config =
        (const weathermap_config_t *)FLASH_PTR(WEATHERMAP_CONFIG_FLASH_OFFSET);
    memcpy(out_config, flash_config, sizeof(weathermap_config_t));

    uint32_t expected_crc = calc_crc32(&out_config->data, sizeof(weathermap_config_data_t));
    if (expected_crc != out_config->crc32) {
        debug_log_with_color(COLOR_YELLOW, "[WMAP CONFIG] CRC mismatch\n");
        return false;
    }
    if (out_config->data.flags != WEATHERMAP_CONFIG_VERSION) {
        debug_log_with_color(COLOR_YELLOW, "[WMAP CONFIG] Version mismatch (0x%08X)\n",
                             out_config->data.flags);
        return false;
    }
    if ((out_config->data.center_lat == 0.0 && out_config->data.center_lon == 0.0) ||
        !isfinite(out_config->data.center_lat) || !isfinite(out_config->data.center_lon) ||
        out_config->data.half_width_m <= 0.0 || !isfinite(out_config->data.half_width_m)) {
        debug_log_with_color(COLOR_YELLOW,
                             "[WMAP CONFIG] Invalid values (lat=%.6f lon=%.6f half=%.2f)\n",
                             out_config->data.center_lat, out_config->data.center_lon,
                             out_config->data.half_width_m);
        return false;
    }
    debug_log("[WMAP CONFIG] Loaded lat=%.6f lon=%.6f half=%.2f\n", out_config->data.center_lat,
              out_config->data.center_lon, out_config->data.half_width_m);
    return true;
}

bool save_weathermap_config(const weathermap_config_t *in_config) {
    weathermap_config_t temp = *in_config;
    temp.data.flags = WEATHERMAP_CONFIG_VERSION;
    temp.crc32 = calc_crc32(&temp.data, sizeof(weathermap_config_data_t));

    const uint32_t sector_offset = WEATHERMAP_CONFIG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_offset, FLASH_SECTOR_SIZE);
    flash_range_program(WEATHERMAP_CONFIG_FLASH_OFFSET, (const uint8_t *)&temp,
                        sizeof(weathermap_config_t));
    restore_interrupts(ints);

    return true;
}

void init_weathermap_config(weathermap_config_t *out_config) {
    if (!load_weathermap_config(out_config)) {
        weathermap_config_t tmp = s_weathermap_defaults;
        save_weathermap_config(&tmp);
        *out_config = tmp;
    }
}
#endif

// ------------------------------
// Device config functions
// ------------------------------
static bool load_device_config(device_config_t *out_config) {
    const device_config_t *flash_config =
        (const device_config_t *)FLASH_PTR(DEVICE_CONFIG_FLASH_OFFSET);
    memcpy(out_config, flash_config, sizeof(device_config_t));

    uint32_t expected_crc = calc_crc32(&out_config->data, sizeof(device_config_data_t));
    return (expected_crc == out_config->crc32);
}

bool save_device_config(const device_config_t *in_config) {
    device_config_t temp = *in_config;
    temp.crc32 = calc_crc32(&temp.data, sizeof(device_config_data_t));

    const uint32_t sector_offset = DEVICE_CONFIG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_offset, FLASH_SECTOR_SIZE);
    flash_range_program(DEVICE_CONFIG_FLASH_OFFSET, (const uint8_t *)&temp,
                        sizeof(device_config_t));
    restore_interrupts(ints);

    return true;
}

void init_device_config(device_config_t *out_config) {
    if (!load_device_config(out_config)) {
        save_device_config(&device_config_flash);
        *out_config = device_config_flash;
    }
}

const void *keep_device_config_flash = &device_config_flash;
const void *keep_wifi_config_flash = &wifi_config_flash;

#ifdef USE_CASE_SEATSURFING
const void *keep_seatsurfing_config_flash = &seatsurfing_config_flash;
#elif defined(USE_CASE_HISTORIAN)
const void *keep_historian_config_flash = &historian_config_flash;
#elif defined(USE_CASE_HOMEMATIC)
const void *keep_homematic_config_flash = &homematic_config_flash;
#elif defined(USE_CASE_WEATHERMAP)
const void *keep_weathermap_config_flash = &weathermap_config_flash;
#endif
