#pragma once

#include <stdint.h>

// Maximum numbers reserved for future expansion (e.g., hostnames)
#define WEATHERMAP_RESERVED_BYTES 32

typedef struct {
    double center_lat;      // degrees
    double center_lon;      // degrees
    double half_width_m;    // meters (half span horizontally)
    uint32_t flags;         // config version / feature flags
    char reserved[WEATHERMAP_RESERVED_BYTES];
} weathermap_config_data_t;

typedef struct {
    weathermap_config_data_t data;
    uint32_t crc32;
} weathermap_config_t;
#define WEATHERMAP_DEFAULT_CENTER_LAT     53.326
#define WEATHERMAP_DEFAULT_CENTER_LON     8.532
#define WEATHERMAP_DEFAULT_HALF_WIDTH_M   6000.0
#define WEATHERMAP_CONFIG_VERSION         0x574D0002u  // 'WM' + version
