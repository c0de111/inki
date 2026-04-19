#pragma once

#include "device_caps.h"
#include "epaper_pages_shared.h"
#include "webserver.h"
#include <stdbool.h>
#include <stddef.h>

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
#define PAGE_DNS_ERROR (-4)
#define PAGE_AUTH_ERROR (-5)
#define PAGE_HTTP_ERROR (-6)
#define PAGE_PARSE_ERROR (-7)

// run_error_t: use-case-internal error classification (private to each client.c).
// Defined here as a shared enum so all use cases speak the same vocabulary.
typedef enum {
    RUN_OK = 0,
    RUN_ERROR_DNS,
    RUN_ERROR_CONNECTION,
    RUN_ERROR_AUTH,
    RUN_ERROR_PARSE,
} run_error_t;

// run_result_t: explicit output of run(). data != NULL on success; error_page is a PAGE_*
// sentinel (negative) on failure and should be used as render_page by main.c.
typedef struct {
    void *data;
    int error_page;
} run_result_t;

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

    // Lifecycle: use case owns data fetch and render.
    // run() returns {data, error_page}: data != NULL on success, error_page set on failure.
    run_result_t (*run)(void);
    void (*render)(uint8_t *image, device_caps_t caps, int page, const void *data);
    void (*free_data)(void *data);

    // Setup webserver: use-case config page and form handler.
    // render_config_page: write HTML form into buf; msg is flash feedback ("" on first load).
    // handle_config_form: parse + save form data; return flash message string (literal).
    void (*render_config_page)(char *buf, size_t sz, const char *msg);
    const char *(*handle_config_form)(const char *body, size_t len);

    // Display capabilities declared by the use case.
    bool needs_4gray;       // true = initialise ePaper in 4Gray mode (e.g. weathermap)
    bool show_display_name; // true = show "Display title" (roomname) field in device settings

    // Extra HTTP routes registered by this use case (NULL-safe; webserver checks
    // extra_route_count).
    const route_t *extra_routes;
    size_t extra_route_count;
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
