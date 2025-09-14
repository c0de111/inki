#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fetch a PNG map from sgx.geodatenzentrum.de over HTTPS (port 443).
// Returns true on success and writes up to max_len bytes into out_buf.
// out_len receives the actual number of bytes written.
bool weathermap_fetch_png(uint8_t* out_buf, size_t max_len, size_t* out_len);

// Fetch but do not store; only count received bytes
bool weathermap_fetch_count(size_t* out_len);

// Boot-time helper: if no map marker present, connect Wi‑Fi, perform count-only fetch,
// log details, and store bytes count marker in flash. Safe to call once at boot.
void weathermap_boot_fetch_if_needed(void);

#ifdef __cplusplus
}
#endif
