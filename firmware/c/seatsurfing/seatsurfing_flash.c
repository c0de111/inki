#include "seatsurfing/seatsurfing_flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

bool save_seatsurfing_config(const seatsurfing_config_t *in) {
    seatsurfing_config_t temp = *in;
    temp.crc32 = calc_crc32(&temp.data, sizeof(seatsurfing_config_data_t));

    const uint32_t sector = UC_CONFIG_FLASH_OFFSET & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector, FLASH_SECTOR_SIZE);
    flash_range_program(UC_CONFIG_FLASH_OFFSET, (const uint8_t *)&temp,
                        sizeof(seatsurfing_config_t));
    restore_interrupts(ints);
    return true;
}

// Linker retention — prevents the XIP-mapped default from being stripped.
const void *keep_seatsurfing_config_flash = &seatsurfing_config_flash;
