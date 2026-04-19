#pragma once

#include <stdint.h>

// Page renderer function signature — all page renderers conform to this type.
typedef void (*page_renderer_t)(uint8_t *image_buffer);

// --- Shared pages (use-case independent) ---

// WiFi setup instructions page
void render_page_wifi_setup(uint8_t *image_buffer);

// Generic placeholder with centered title
void render_page_placeholder(uint8_t *image_buffer, const char *title);

// Fallback for unassigned page index
void render_page_fallback(int page, uint8_t *image_buffer);

// Error pages (server error, WiFi error, etc.)
void render_page_error(uint8_t *image_buffer, const char *title, const char *detail,
                       const char *tip);

// --- Shared page_def_t constants (defined in epaper_pages_shared.c, type in use_case.h) ---
struct page_def;
extern const struct page_def page_dnd;
extern const struct page_def page_decision_maker;
extern const struct page_def page_device_info;
extern const struct page_def page_nfc_text;
extern const struct page_def page_nfc_image;
extern const struct page_def page_wifi_setup;
extern const struct page_def page_placeholder;
