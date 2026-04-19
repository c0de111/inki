#include "historian/historian_flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

bool save_historian_config(const historian_config_t *in) {
    historian_config_t temp = *in;
    temp.crc32 = calc_crc32(&temp.data, sizeof(historian_config_data_t));

    const uint32_t sector = UC_CONFIG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector, FLASH_SECTOR_SIZE);
    flash_range_program(UC_CONFIG_FLASH_OFFSET, (const uint8_t *)&temp, sizeof(historian_config_t));
    restore_interrupts(ints);
    return true;
}

// Linker retention — prevents the XIP-mapped default from being stripped.
const void *keep_historian_config_flash = &historian_config_flash;
