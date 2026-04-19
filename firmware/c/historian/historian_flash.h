#pragma once
#include "flash.h"
#include "historian/config.h"
#include <stdbool.h>

#define historian_config_flash (*(const historian_config_t *)FLASH_PTR(UC_CONFIG_FLASH_OFFSET))

bool save_historian_config(const historian_config_t *in);
