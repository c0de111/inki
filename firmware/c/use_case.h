#pragma once

#include "epaper_pages_shared.h"
#include <stdbool.h>

// --- Page definition ---

typedef struct page_def {
    const char *name;
    page_renderer_t render;
    bool needs_wifi;
} page_def_t;

// --- Input mapping ---

// Source namespaces: buttons 0-7, NFC opcodes 0x100+, future web commands 0x200+
#define INPUT_BUTTON(n) (n)
#define INPUT_NFC(op) (0x100 | (op))

// Sentinel / special values
#define PAGE_ACTION_SETUP (-1)
#define INPUT_MAP_END (-2)
#define PAGE_WIFI_ERROR (-3)

typedef struct {
    int source;
    int page_index; // index into pages[], or PAGE_ACTION_SETUP
} input_map_entry_t;

// --- Use case contract ---

typedef struct {
    const char *name;

    // Declarative page catalog and input mapping
    const page_def_t **pages;           // NULL-terminated catalog
    const input_map_entry_t *input_map; // sentinel-terminated (INPUT_MAP_END)
    int default_page;

    // Lifecycle: use case owns data fetch and render
    void *(*run)(void);
    void (*render)(uint8_t *image, float battery_voltage, int page, const void *data);
    void (*free_data)(void *data);
} use_case_t;

// Linker-resolved: exactly one use case is compiled in per firmware build.
extern const use_case_t use_case;

// --- Shared dispatch helpers ---

// Resolve an input source (INPUT_BUTTON/INPUT_NFC) to a page index via use_case.input_map.
static inline int resolve_page_index(int source) {
    for (const input_map_entry_t *m = use_case.input_map; m->source != INPUT_MAP_END; m++) {
        if (m->source == source) {
            return m->page_index;
        }
    }
    return use_case.default_page;
}
