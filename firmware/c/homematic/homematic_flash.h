#pragma once
#include "flash.h"
#include "homematic/config.h"
#include <stdbool.h>

#define homematic_config_flash (*(const homematic_config_t *)FLASH_PTR(UC_CONFIG_FLASH_OFFSET))

bool save_homematic_config(const homematic_config_t *in);
