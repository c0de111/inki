#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Streamed PNG decoder: decodes from XIP pointer (PNG bytes) and writes
// rows directly as 2‑bpp grayscale into the weathermap image flash region.
// Returns true on success.
bool png_stream_decode_to_flash_from_xip(const uint8_t *png, size_t png_len);

// Stream-decode PNG from XIP and draw directly to current Paint buffer at (off_x, off_y)
bool png_stream_draw_to_paint_from_xip(const uint8_t *png, size_t png_len, uint16_t off_x,
                                       uint16_t off_y);

#ifdef __cplusplus
}
#endif
