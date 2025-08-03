#pragma once
#include <stdint.h>
#include "rooms.h"

typedef struct __attribute__((packed)) {
    char ssid[32];
    char password[64];
    uint32_t crc32;
} wifi_config_t;
