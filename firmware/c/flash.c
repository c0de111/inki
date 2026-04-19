#include "pico/flash.h"
#define LOG_MODULE LOG_MOD_FLASH
#include "debug.h"
#include "flash.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/m0plus.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/time.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

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

uint32_t calc_crc32(const void *data, size_t len) {
    init_crc32_table();
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *buf = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

bool get_firmware_slot_info(uint8_t slot, firmware_slot_info_t *info) {
    uint32_t offset = (slot == 0)   ? FIRMWARE_SLOT0_FLASH_OFFSET
                      : (slot == 1) ? FIRMWARE_SLOT1_FLASH_OFFSET
                                    : 0;

    if (offset == 0)
        return false;

    const firmware_header_t *header = (const firmware_header_t *)FLASH_PTR(offset);

    if (memcmp(header->magic, FIRMWARE_MAGIC, sizeof(header->magic)) != 0 ||
        header->valid_flag != 1)
        return false;

    strncpy(info->build_date, header->build_date, sizeof(info->build_date) - 1);
    info->build_date[sizeof(info->build_date) - 1] = '\0';
    strncpy(info->git_version, header->git_version, sizeof(info->git_version) - 1);
    info->git_version[sizeof(info->git_version) - 1] = '\0';
    info->size = header->firmware_size;
    info->crc32 = header->crc32;
    info->slot_index = header->slot;
    info->valid_flag = header->valid_flag;

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

void flash_log_status(void) {
    const char *active = get_active_firmware_slot_info();
    debug_status("OK", "Running from: %s\n", active);

    for (uint8_t slot = 0; slot < 2; slot++) {
        firmware_slot_info_t info = {0};
        bool has = get_firmware_slot_info(slot, &info);
        if (has) {
            debug_status("OK", "Slot %d: %s, Build %s, %u bytes\n", slot, info.git_version,
                         info.build_date, info.size);
        } else {
            debug_status("WARN", "Slot %d: no valid firmware\n", slot);
        }
    }
}

bool save_uploaded_logo_to_flash(const uint8_t *data, size_t len) {
    if (len < 18) {
        dlog("Logo upload failed: data too short (%d bytes)\n", len);
        return false;
    }

    if (memcmp(data, "LOGO", 4) != 0) {
        dlog("Logo upload failed: invalid magic header\n");
        return false;
    }

    uint16_t width = data[4] | (data[5] << 8);
    uint16_t height = data[6] | (data[7] << 8);
    uint32_t datalen = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);
    size_t expected = datalen + 18;

    if (expected != len) {
        dlog("Logo upload failed: datalen mismatch (%d + 18 != %d)\n", datalen, len);
        return false;
    }

    if (len > LOGO_FLASH_SIZE) {
        dlog("Logo upload failed: file too large (%d > %d bytes)\n", len, LOGO_FLASH_SIZE);
        return false;
    }

    dlog("Logo upload OK: %dx%d px, %d bytes total\n", width, height, len);

    static uint8_t padded[LOGO_FLASH_SIZE];
    memset(padded, 0xFF, sizeof(padded));
    memcpy(padded, data, len);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(LOGO_FLASH_OFFSET, LOGO_FLASH_SIZE);
    flash_range_program(LOGO_FLASH_OFFSET, padded, LOGO_FLASH_SIZE);
    restore_interrupts(ints);

    dlog("Logo written to Flash at offset 0x%X\n", LOGO_FLASH_OFFSET);
    return true;
}

void flash_erase_logo(void) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(LOGO_FLASH_OFFSET, LOGO_FLASH_SIZE);
    restore_interrupts(ints);
    dlog("FLASH: logo sector erased\n");
}

void flash_zero_settings_sector(void) {
    const uint32_t sector_offset = UC_CONFIG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);
    dlog("FLASH: erasing settings sector at 0x%X\n", sector_offset);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_offset, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
}

bool flash_demote_slot_header(uint32_t header_flash_offset) {
    const uint32_t sector_base = header_flash_offset & ~(FLASH_SECTOR_SIZE - 1);
    const size_t hdr_off = (size_t)(header_flash_offset - sector_base);

    if (hdr_off + sizeof(firmware_header_t) > FLASH_SECTOR_SIZE) {
        dlog("DEMOTE SLOT: header crosses sector boundary at 0x%08X\n", header_flash_offset);
        return false;
    }

    uint8_t sector_buffer[FLASH_SECTOR_SIZE];
    memcpy(sector_buffer, FLASH_PTR(sector_base), FLASH_SECTOR_SIZE);

    firmware_header_t *header = (firmware_header_t *)(sector_buffer + hdr_off);
    if (memcmp(header->magic, FIRMWARE_MAGIC, FIRMWARE_MAGIC_LEN) != 0 || header->valid_flag != 1) {
        dlog("DEMOTE SLOT: invalid header at 0x%08X\n", header_flash_offset);
        return false;
    }

    dlog("DEMOTE SLOT: before slot=%u version='%.*s' build='%.*s'\n", (unsigned)header->slot,
         (int)sizeof(header->git_version), header->git_version, (int)sizeof(header->build_date),
         header->build_date);

    memset(header->git_version, 0, sizeof(header->git_version));
    memset(header->build_date, 0, sizeof(header->build_date));
    snprintf(header->git_version, sizeof(header->git_version), "v0.0.0-0");
    snprintf(header->build_date, sizeof(header->build_date), "1970-01-01");

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_base, FLASH_SECTOR_SIZE);
    flash_range_program(sector_base, sector_buffer, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);

    dlog("DEMOTE SLOT: after slot=%u version='%.*s' build='%.*s' (hdr=0x%08X sector=0x%08X)\n",
         (unsigned)header->slot, (int)sizeof(header->git_version), header->git_version,
         (int)sizeof(header->build_date), header->build_date, header_flash_offset, sector_base);
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

const void *keep_device_config_flash = &device_config_flash;
const void *keep_wifi_config_flash = &wifi_config_flash;

// =============================================================================
// OTA STREAMING API
// Must be called from Thread mode (main event loop), NOT from recv_cb.
// sleep_ms() after flash operations is effective only from Thread mode.
// =============================================================================

static bool flash_mark_firmware_valid(uint32_t slot_offset); // defined below

static struct {
    uint32_t slot_offset;  // base address of target firmware slot
    uint32_t write_cursor; // next flash address to program
    uint32_t erase_cursor; // next flash address to erase
    uint32_t erase_end;    // end of erase region
} s_ota;

void flash_ota_begin(uint32_t slot_offset, size_t erase_length) {
    s_ota.slot_offset = slot_offset;
    s_ota.write_cursor = slot_offset;
    s_ota.erase_cursor = slot_offset;
    s_ota.erase_end = slot_offset + erase_length;
}

// Returns true when the last sector has been erased.
bool flash_ota_erase_next_sector(void) {
    if (s_ota.erase_cursor >= s_ota.erase_end)
        return true;

    watchdog_update();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(s_ota.erase_cursor, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    s_ota.erase_cursor += FLASH_SECTOR_SIZE;

    return (s_ota.erase_cursor >= s_ota.erase_end);
}

// Write exactly FLASH_PAGE_SIZE bytes.  Caller must pad the last partial page with 0xFF.
void flash_ota_write_page(const uint8_t *data) {
    watchdog_update();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(s_ota.write_cursor, data, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    s_ota.write_cursor += FLASH_PAGE_SIZE;
}

// Validate header + CRC; mark slot valid if all checks pass.
// Call this after every page (including the last padded one) has been written.
flash_ota_result_t flash_ota_finish(void) {
    flash_ota_result_t r = {0};

    const firmware_header_t *hdr = (const firmware_header_t *)FLASH_PTR(s_ota.slot_offset);
    memcpy(&r.header, hdr, sizeof(r.header));

    r.valid = (memcmp(hdr->magic, FIRMWARE_MAGIC, FIRMWARE_MAGIC_LEN) == 0);
    uint8_t expected_slot = (s_ota.slot_offset == FIRMWARE_SLOT0_FLASH_OFFSET) ? 0 : 1;
    r.slot_ok = (hdr->slot == expected_slot);
    // CRC covers firmware_size bytes starting after the 256-byte header.
    if (r.header.firmware_size > sizeof(firmware_header_t)) {
        const uint8_t *payload = FLASH_PTR(s_ota.slot_offset) + sizeof(firmware_header_t);
        size_t payload_len = r.header.firmware_size - sizeof(firmware_header_t);
        r.actual_crc = calc_crc32(payload, payload_len);
    }
    r.crc_ok = (r.actual_crc == hdr->crc32);

    if (r.valid && r.crc_ok && r.slot_ok)
        flash_mark_firmware_valid(s_ota.slot_offset);

    return r;
}

// Rewrite the first sector of the slot with valid_flag = 1.
static bool flash_mark_firmware_valid(uint32_t slot_offset) {
    uint8_t sector_buf[FLASH_SECTOR_SIZE];
    memcpy(sector_buf, FLASH_PTR(slot_offset), FLASH_SECTOR_SIZE);

    firmware_header_t *hdr = (firmware_header_t *)sector_buf;
    if (memcmp(hdr->magic, FIRMWARE_MAGIC, FIRMWARE_MAGIC_LEN) != 0)
        return false;

    hdr->valid_flag = 1;

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(slot_offset, FLASH_SECTOR_SIZE);
    flash_range_program(slot_offset, sector_buf, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);

    dlog("[OTA] Slot at 0x%08X marked valid (v=%.*s)\n", slot_offset, (int)sizeof(hdr->git_version),
         hdr->git_version);
    return true;
}
