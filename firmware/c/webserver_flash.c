#include "webserver_flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include <string.h>
#include <stdio.h>
#include "debug.h"
#include "flash.h"
#include "webserver.h"
#include "webserver_utils.h"

void mark_firmware_valid(uint32_t flash_offset) {
    // Read the 4 KB flash sector that contains the firmware header
    uint8_t sector_buffer[FLASH_SECTOR_SIZE];
    memcpy(sector_buffer, FLASH_PTR(flash_offset), FLASH_SECTOR_SIZE);

    // Patch the valid_flag inside the firmware_header_t
    firmware_header_t* header = (firmware_header_t*)sector_buffer;

    debug_log("Firmware header before setting valid_flag:\n");
    debug_log("  magic         : '%.*s'\n", (int)sizeof(header->magic), header->magic);
    debug_log("  valid_flag    : %u\n", header->valid_flag);
    debug_log("  build_date    : '%.*s'\n", (int)sizeof(header->build_date), header->build_date);
    debug_log("  git_version   : '%.*s'\n", (int)sizeof(header->git_version), header->git_version);
    debug_log("  firmware_size : %u\n", header->firmware_size);
    debug_log("  slot          : %u\n", header->slot);
    debug_log("  crc32         : 0x%08X\n", header->crc32);

    header->valid_flag = 1;

    // Erase and reprogram the 4 KB flash sector
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);
    flash_range_program(flash_offset, sector_buffer, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);

    debug_log("Firmware marked valid (sector-based rewrite).\n");
}

void flush_page_to_flash() {
    if (flash_writer.buffer_filled == 0) {
        debug_log("FLASH: flush_page_to_flash() called, but buffer is empty – skipping\n");
        return;
    }
    watchdog_update();

    // Padding if needed
    if (flash_writer.buffer_filled % FLASH_PAGE_SIZE != 0) {
        size_t pad_size = FLASH_PAGE_SIZE - flash_writer.buffer_filled;
        memset(flash_writer.buffer + flash_writer.buffer_filled, 0xFF, pad_size);
        debug_log("FLASH: padding %u bytes with 0xFF\n", (unsigned)pad_size);
    }

    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(flash_writer.flash_offset,
                        flash_writer.buffer,
                        FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    flash_writer.flash_offset += FLASH_PAGE_SIZE;
    flash_writer.buffer_filled = 0;
}