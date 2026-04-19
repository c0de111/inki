#pragma once
#include "flash.h"
#include "seatsurfing/config.h"
#include <stdbool.h>

#define seatsurfing_config_flash (*(const seatsurfing_config_t *)FLASH_PTR(UC_CONFIG_FLASH_OFFSET))

bool save_seatsurfing_config(const seatsurfing_config_t *in);
