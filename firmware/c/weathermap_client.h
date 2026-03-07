#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fetch a PNG map from sgx.geodatenzentrum.de over HTTPS (port 443).
// Returns true on success and writes up to max_len bytes into out_buf.
// out_len receives the actual number of bytes written.
bool weathermap_fetch_png(uint8_t *out_buf, size_t max_len, size_t *out_len);

// Fetch but do not store; only count received bytes
bool weathermap_fetch_count(size_t *out_len);

// Boot-time helper: connect Wi‑Fi, fetch geodata via HTTPS and stage PNG into flash.
// Currently fetches from a configured provider (DWD preferred, BKG fallback).
// Later this can become conditional (static basemap vs. periodically refreshed radar).
void geodata_fetch(void);

// Render the stored 2-bit image from flash onto the current ePaper canvas using Paint_SetPixel.
// Returns false if no valid image is stored.
bool weathermap_render_from_flash(void);

#ifdef __cplusplus
}
#endif
