#include "homematic/homematic_flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

bool save_homematic_config(const homematic_config_t *in) {
    homematic_config_t temp = *in;
    temp.crc32 = calc_crc32(&temp.data, sizeof(homematic_config_data_t));

    const uint32_t sector = UC_CONFIG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector, FLASH_SECTOR_SIZE);
    flash_range_program(UC_CONFIG_FLASH_OFFSET, (const uint8_t *)&temp, sizeof(homematic_config_t));
    restore_interrupts(ints);
    return true;
}

// Linker retention — prevents the XIP-mapped default from being stripped.
const void *keep_homematic_config_flash = &homematic_config_flash;
