#pragma once
#include <stdint.h>
#include <stdbool.h>

#define HOMEMATIC_MAX_ITEMS 6

typedef struct {
    char address[40];        // e.g., "002820C98F3853:1" or with prefix
    char key[32];            // e.g., "ACTUAL_TEMPERATURE", "STATE"
    char fallback_label[16]; // optional display label until auto-label is implemented
} homematic_item_t;

typedef struct {
    uint8_t ip[4];
    uint16_t port;               // default 2010
    bool add_interface_prefix;   // prepend "HmIP-RF." when true (if missing)
    bool auto_label;             // try to fetch labels from CCU
    uint8_t count;               // number of active items in items[]
    homematic_item_t items[HOMEMATIC_MAX_ITEMS];
} homematic_config_data_t;

typedef struct {
    homematic_config_data_t data;
    uint32_t crc32;
} homematic_config_t;

