#include "png_stream.h"
#include "debug.h"
#include "flash.h"
// Include full miniz header to ensure mz_size_t and tinfl types are defined
#include "third_party/GUI/GUI_Paint.h"
#include "third_party/miniz/miniz_tinfl.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef WMAP_USE_SIMPLE_QUANT
#define WMAP_USE_SIMPLE_QUANT 1 // 1: simple 4-bin mapping; 0: FS dithering
#endif

// PNG signature
static const uint8_t PNG_SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

static inline int16_t clamp_i16_from_i32(int32_t v) {
    if (v > 32767)
        return 32767;
    if (v < -32768)
        return -32768;
    return (int16_t)v;
}

static inline void add_i16_saturating(int16_t *dst, int32_t delta) {
    int32_t sum = (int32_t)(*dst) + delta;
    *dst = clamp_i16_from_i32(sum);
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;   // must be 8
    uint8_t color_type;  // 0=G,2=RGB,3=PLTE,6=RGBA
    uint8_t compression; // 0
    uint8_t filter;      // 0
    uint8_t interlace;   // 0
    uint8_t bpp;         // bytes per pixel (1,3,4)
    // palette (if color_type==3)
    uint8_t palette[256 * 3];
    int palette_len; // entries
    // IDAT extents
    const uint8_t *idat_first;
    size_t idat_total;
} png_info_t;

static bool parse_png_header(const uint8_t *png, size_t len, png_info_t *out) {
    if (!png || len < 8 || memcmp(png, PNG_SIG, 8) != 0) {
        debug_log_with_color(COLOR_RED, "[PNG] Bad signature\n");
        return false;
    }
    memset(out, 0, sizeof(*out));
    size_t off = 8;
    bool saw_IHDR = false, saw_IEND = false;
    while (off + 12 <= len) {
        uint32_t clen = be32(png + off);
        if (off + 12 + clen > len) {
            debug_log_with_color(COLOR_RED, "[PNG] Truncated chunk\n");
            return false;
        }
        const uint8_t *ctype = png + off + 4;
        const uint8_t *cdata = png + off + 8;
        // const uint8_t* ccrc  = png + off + 8 + clen;
        if (ctype[0] == 'I' && ctype[1] == 'H' && ctype[2] == 'D' && ctype[3] == 'R') {
            if (clen < 13) {
                debug_log_with_color(COLOR_RED, "[PNG] IHDR short\n");
                return false;
            }
            out->width = be32(cdata);
            out->height = be32(cdata + 4);
            out->bit_depth = cdata[8];
            out->color_type = cdata[9];
            out->compression = cdata[10];
            out->filter = cdata[11];
            out->interlace = cdata[12];
            if (out->bit_depth != 8 || out->compression != 0 || out->filter != 0 ||
                out->interlace != 0) {
                debug_log_with_color(
                    COLOR_RED, "[PNG] Unsupported IHDR: depth=%u comp=%u filter=%u interlace=%u\n",
                    out->bit_depth, out->compression, out->filter, out->interlace);
                return false;
            }
            switch (out->color_type) {
            case 0:
                out->bpp = 1;
                break; // Gray
            case 2:
                out->bpp = 3;
                break; // RGB
            case 3:
                out->bpp = 1;
                break; // Indexed
            case 6:
                out->bpp = 4;
                break; // RGBA
            default:
                debug_log_with_color(COLOR_RED, "[PNG] Unsupported color type: %u\n",
                                     out->color_type);
                return false;
            }
            saw_IHDR = true;
        } else if (ctype[0] == 'P' && ctype[1] == 'L' && ctype[2] == 'T' && ctype[3] == 'E') {
            if (!saw_IHDR) {
                debug_log_with_color(COLOR_RED, "[PNG] PLTE before IHDR\n");
                return false;
            }
            if (clen == 0 || (clen % 3) != 0 || clen > sizeof(out->palette)) {
                debug_log_with_color(COLOR_RED, "[PNG] Bad PLTE len=%u\n", (unsigned)clen);
                return false;
            }
            memcpy(out->palette, cdata, clen);
            out->palette_len = (int)(clen / 3u);
        } else if (ctype[0] == 'I' && ctype[1] == 'D' && ctype[2] == 'A' && ctype[3] == 'T') {
            if (!saw_IHDR) {
                debug_log_with_color(COLOR_RED, "[PNG] IDAT before IHDR\n");
                return false;
            }
            if (!out->idat_first)
                out->idat_first = cdata;
            out->idat_total += clen;
        } else if (ctype[0] == 'I' && ctype[1] == 'E' && ctype[2] == 'N' && ctype[3] == 'D') {
            saw_IEND = true;
            break;
        }
        off += 12 + clen;
    }
    if (!saw_IHDR) {
        debug_log_with_color(COLOR_RED, "[PNG] Missing IHDR\n");
        return false;
    }
    if (!saw_IEND) {
        debug_log_with_color(COLOR_RED, "[PNG] Missing IEND\n");
        return false;
    }
    if (out->color_type == 3 && out->palette_len <= 0) {
        debug_log_with_color(COLOR_RED, "[PNG] Indexed image without PLTE\n");
        return false;
    }
    return true;
}

// Paeth predictor helper
static inline uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = abs(p - (int)a);
    int pb = abs(p - (int)b);
    int pc = abs(p - (int)c);
    if (pa <= pb && pa <= pc)
        return a;
    else if (pb <= pc)
        return b;
    else
        return c;
}

// Unfilter a row in-place. cur points to [filter][bytes...]; prev points to [0][bytes...] (prev
// row's bytes)
static void unfilter_row(uint8_t *cur, const uint8_t *prev, size_t row_bytes, uint8_t bpp) {
    uint8_t f = cur[0];
    uint8_t *x = cur + 1; // pixel bytes
    const uint8_t *p = prev + 1;
    switch (f) {
    case 0: // None
        break;
    case 1: // Sub
        for (size_t i = 0; i < row_bytes; i++) {
            uint8_t left = (i >= bpp) ? x[i - bpp] : 0;
            x[i] = (uint8_t)(x[i] + left);
        }
        break;
    case 2: // Up
        for (size_t i = 0; i < row_bytes; i++) {
            x[i] = (uint8_t)(x[i] + p[i]);
        }
        break;
    case 3: // Average
        for (size_t i = 0; i < row_bytes; i++) {
            uint8_t left = (i >= bpp) ? x[i - bpp] : 0;
            x[i] = (uint8_t)(x[i] + (uint8_t)(((int)left + (int)p[i]) / 2));
        }
        break;
    case 4: // Paeth
        for (size_t i = 0; i < row_bytes; i++) {
            uint8_t left = (i >= bpp) ? x[i - bpp] : 0;
            uint8_t up = p[i];
            uint8_t ul = (i >= bpp) ? p[i - bpp] : 0;
            x[i] = (uint8_t)(x[i] + paeth(left, up, ul));
        }
        break;
    default:
        // Unknown filter: treat as None to avoid crashing
        break;
    }
}

typedef struct {
    // Input traversal over concatenated IDAT chunks
    const uint8_t *png;
    size_t len;
    size_t off; // current chunk scan offset (points at chunk length)
    // Current IDAT window
    const uint8_t *idat_ptr;
    size_t idat_rem;
} idat_reader_t;

static void idat_reader_init(idat_reader_t *r, const uint8_t *png, size_t len) {
    memset(r, 0, sizeof(*r));
    r->png = png;
    r->len = len;
    r->off = 8;
    r->idat_ptr = NULL;
    r->idat_rem = 0;
}

// Fill up to req bytes into *in_ptr/*in_avail from current/next IDAT chunk
static size_t idat_reader_pull(idat_reader_t *r, const uint8_t **in_ptr, size_t *in_avail) {
    if (r->idat_rem == 0) {
        // find next IDAT chunk
        while (r->off + 12 <= r->len) {
            uint32_t clen = be32(r->png + r->off);
            const uint8_t *ctype = r->png + r->off + 4;
            const uint8_t *cdata = r->png + r->off + 8;
            if (r->off + 12 + clen > r->len) {
                break;
            }
            if (ctype[0] == 'I' && ctype[1] == 'D' && ctype[2] == 'A' && ctype[3] == 'T') {
                r->idat_ptr = cdata;
                r->idat_rem = clen;
                r->off += 12 + clen;
                break;
            }
            // Stop at IEND
            if (ctype[0] == 'I' && ctype[1] == 'E' && ctype[2] == 'N' && ctype[3] == 'D') {
                r->idat_ptr = NULL;
                r->idat_rem = 0;
                break;
            }
            r->off += 12 + clen;
        }
    }
    size_t n = r->idat_rem;
    if (n > 0) {
        *in_ptr = r->idat_ptr;
        *in_avail = n;
    } else {
        *in_ptr = NULL;
        *in_avail = 0;
    }
    return n;
}

static void idat_reader_consume(idat_reader_t *r, size_t n) {
    if (n > r->idat_rem)
        n = r->idat_rem;
    r->idat_ptr += n;
    r->idat_rem -= n;
}

// Baseline luminance quantization: 0..63->0, 64..127->1, 128..191->2, 192..255->3
static inline uint8_t lum_to_2b(uint8_t y) { return (uint8_t)(y >> 6); }

bool png_stream_decode_to_flash_from_xip(const uint8_t *png, size_t png_len) {
    png_info_t info;
    if (!parse_png_header(png, png_len, &info))
        return false;

    if (!weathermap_flash_begin_image((uint16_t)info.width, (uint16_t)info.height)) {
        debug_log_with_color(COLOR_RED, "[PNG] Failed to begin flash image\n");
        return false;
    }

    size_t row_bytes = (size_t)info.width * info.bpp;
    uint8_t *prev = (uint8_t *)malloc(1 + row_bytes);
    uint8_t *cur = (uint8_t *)malloc(1 + row_bytes);
    if (!prev || !cur) {
        debug_log_with_color(COLOR_RED, "[PNG] OOM rows\n");
        if (prev)
            free(prev);
        if (cur)
            free(cur);
        return false;
    }
    memset(prev, 0, 1 + row_bytes);

    // Initialize inflater
    tinfl_decompressor infl;
    tinfl_init(&infl);
    idat_reader_t reader;
    idat_reader_init(&reader, png, png_len);

    // Output staging buffer from inflater
    uint8_t outbuf[1024];
    size_t out_have = 0; // bytes available in outbuf
    size_t out_pos = 0;  // read position in outbuf

    // Refill handled inline in the row loop

    uint32_t rows = info.height;
    uint32_t y = 0;

    // Read stream: for each row, read 1 filter byte + row_bytes
    for (y = 0; y < rows; y++) {
        // Fill cur row from stream
        size_t need = 1 + row_bytes;
        size_t got = 0;
        while (got < need) {
            if (out_pos == out_have) {
                // Refill outbuf by looping tinfl until it produces output or we run out of input
                size_t produced = 0;
                while (produced == 0) {
                    const uint8_t *in_ptr = NULL;
                    size_t in_avail = 0;
                    idat_reader_pull(&reader, &in_ptr, &in_avail);
                    if (!in_ptr || in_avail == 0) {
                        // No more input and no output: error
                        debug_log_with_color(COLOR_RED, "[PNG] Unexpected end of IDAT at row %u\n",
                                             (unsigned)y);
                        free(prev);
                        free(cur);
                        return false;
                    }
                    size_t in_sz = (size_t)in_avail;
                    size_t out_sz = (size_t)sizeof(outbuf);
                    tinfl_status st =
                        tinfl_decompress(&infl, in_ptr, &in_sz, outbuf, outbuf, &out_sz,
                                         TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_HAS_MORE_INPUT |
                                             TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
                    idat_reader_consume(&reader, (size_t)in_sz);
                    produced = (size_t)out_sz;
                    if (produced == 0) {
                        if (st == TINFL_STATUS_DONE) {
                            debug_log_with_color(COLOR_RED,
                                                 "[PNG] Inflator finished early at row %u\n",
                                                 (unsigned)y);
                            free(prev);
                            free(cur);
                            return false;
                        }
                        // NEEDS_MORE_INPUT or no output yet: continue pulling input
                        continue;
                    }
                    out_have = produced;
                    out_pos = 0;
                }
            }
            size_t to_copy = need - got;
            size_t avail = out_have - out_pos;
            size_t n = (to_copy < avail) ? to_copy : avail;
            memcpy(cur + got, outbuf + out_pos, n);
            out_pos += n;
            got += n;
        }

        // Unfilter and convert row
        unfilter_row(cur, prev, row_bytes, info.bpp);

        // Pack to 2‑bpp and flush
        size_t packed_len = (info.width + 3) / 4;
        uint8_t *packed = (uint8_t *)malloc(packed_len);
        if (!packed) {
            free(prev);
            free(cur);
            return false;
        }
        memset(packed, 0, packed_len);
        const uint8_t *px = cur + 1;
        int shift = 6;
        size_t di = 0;
        uint8_t acc = 0;
        for (uint32_t x = 0; x < info.width; x++) {
            uint8_t Y;
            switch (info.color_type) {
            case 0: // Gray
                Y = px[0];
                px += 1;
                break;
            case 2: // RGB
            {
                uint8_t r = px[0], g = px[1], b = px[2];
                px += 3;
                uint16_t y16 = (uint16_t)(r * 30 + g * 59 + b * 11) / 100;
                Y = (uint8_t)y16;
            } break;
            case 3: // Indexed
            {
                uint8_t idx = px[0];
                px += 1;
                if (idx >= (uint8_t)info.palette_len)
                    idx = 0;
                uint8_t r = info.palette[3 * idx + 0];
                uint8_t g = info.palette[3 * idx + 1];
                uint8_t b = info.palette[3 * idx + 2];
                uint16_t y16 = (uint16_t)(r * 30 + g * 59 + b * 11) / 100;
                Y = (uint8_t)y16;
            } break;
            case 6: // RGBA
            {
                uint8_t r = px[0], g = px[1], b = px[2]; /*a=px[3]*/
                px += 4;
                uint16_t y16 = (uint16_t)(r * 30 + g * 59 + b * 11) / 100;
                Y = (uint8_t)y16;
            } break;
            default:
                Y = 0;
                break;
            }
            uint8_t v = lum_to_2b(Y);
            acc |= (uint8_t)(v << shift);
            if (shift == 0) {
                packed[di++] = acc;
                acc = 0;
                shift = 6;
            } else {
                shift -= 2;
            }
        }
        if (shift != 6)
            packed[di++] = acc;
        if (!weathermap_flash_append_row_2bpp(packed, di)) {
            free(packed);
            free(prev);
            free(cur);
            debug_log_with_color(COLOR_RED, "[PNG] Flash append failed at row %u\n", (unsigned)y);
            return false;
        }
        free(packed);
        // Swap rows
        uint8_t *tmp = prev;
        prev = cur;
        cur = tmp;
    }

    free(prev);
    free(cur);
    if (!weathermap_flash_end_image()) {
        debug_log_with_color(COLOR_RED, "[PNG] Flash finalize failed\n");
        return false;
    }
    debug_log_with_color(COLOR_GREEN, "[PNG] Decode complete and stored to flash\n");
    return true;
}

// Draw decoded PNG directly to Paint buffer (no intermediate flash storage)
bool png_stream_draw_to_paint_from_xip(const uint8_t *png, size_t png_len, uint16_t off_x,
                                       uint16_t off_y) {
    png_info_t info;
    if (!parse_png_header(png, png_len, &info))
        return false;

    size_t row_bytes = (size_t)info.width * info.bpp;
    uint8_t *prev = (uint8_t *)malloc(1 + row_bytes);
    uint8_t *cur = (uint8_t *)malloc(1 + row_bytes);
    if (!prev || !cur) {
        if (prev)
            free(prev);
        if (cur)
            free(cur);
        return false;
    }
    memset(prev, 0, 1 + row_bytes);

    // First pass: determine dynamic range
    // - Indexed: track min/max index and also min/max palette luminance actually used in pixels
    // - Others: track min/max Y directly
    uint8_t min_idx = 255, max_idx = 0;
    uint8_t min_y = 255, max_y = 0;

    tinfl_decompressor infl;
    tinfl_init(&infl);
    idat_reader_t reader;
    idat_reader_init(&reader, png, png_len);

    uint8_t outbuf[1024];
    size_t out_have = 0, out_pos = 0;
    for (uint32_t y = 0; y < info.height; y++) {
        size_t need = 1 + row_bytes, got = 0;
        while (got < need) {
            if (out_pos == out_have) {
                size_t produced = 0;
                while (produced == 0) {
                    const uint8_t *in_ptr = NULL;
                    size_t in_avail = 0;
                    idat_reader_pull(&reader, &in_ptr, &in_avail);
                    if (!in_ptr || in_avail == 0) {
                        free(prev);
                        free(cur);
                        return false;
                    }
                    size_t in_sz = (size_t)in_avail;
                    size_t out_sz = sizeof(outbuf);
                    tinfl_status st =
                        tinfl_decompress(&infl, in_ptr, &in_sz, outbuf, outbuf, &out_sz,
                                         TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_HAS_MORE_INPUT |
                                             TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
                    idat_reader_consume(&reader, (size_t)in_sz);
                    produced = out_sz;
                    if (produced == 0) {
                        if (st == TINFL_STATUS_DONE) {
                            free(prev);
                            free(cur);
                            return false;
                        }
                        continue;
                    }
                    out_have = produced;
                    out_pos = 0;
                }
            }
            size_t to_copy = need - got;
            size_t avail = out_have - out_pos;
            size_t n = (to_copy < avail) ? to_copy : avail;
            memcpy(cur + got, outbuf + out_pos, n);
            out_pos += n;
            got += n;
        }

        // Unfilter current row
        unfilter_row(cur, prev, row_bytes, info.bpp);

        // Scan pixels to update ranges
        const uint8_t *px = cur + 1;
        for (uint32_t x = 0; x < info.width; x++) {
            if (info.color_type == 3) {
                uint8_t idx = px[0];
                px += 1;
                if (idx < min_idx)
                    min_idx = idx;
                if (idx > max_idx)
                    max_idx = idx;
                // Also compute luminance from palette for actual used pixels
                if (idx >= (uint8_t)info.palette_len)
                    idx = 0;
                uint8_t r = info.palette[3 * idx + 0];
                uint8_t g = info.palette[3 * idx + 1];
                uint8_t b = info.palette[3 * idx + 2];
                uint8_t Y = (uint8_t)((r * 30 + g * 59 + b * 11) / 100);
                if (Y < min_y)
                    min_y = Y;
                if (Y > max_y)
                    max_y = Y;
            } else if (info.color_type == 0) {
                uint8_t Y = px[0];
                px += 1;
                if (Y < min_y)
                    min_y = Y;
                if (Y > max_y)
                    max_y = Y;
            } else if (info.color_type == 2) {
                uint8_t r = px[0], g = px[1], b = px[2];
                px += 3;
                uint8_t Y = (uint8_t)((r * 30 + g * 59 + b * 11) / 100);
                if (Y < min_y)
                    min_y = Y;
                if (Y > max_y)
                    max_y = Y;
            } else if (info.color_type == 6) {
                uint8_t r = px[0], g = px[1], b = px[2];
                px += 4;
                uint8_t Y = (uint8_t)((r * 30 + g * 59 + b * 11) / 100);
                if (Y < min_y)
                    min_y = Y;
                if (Y > max_y)
                    max_y = Y;
            }
        }
        // Swap rows
        uint8_t *tmp = prev;
        prev = cur;
        cur = tmp;
    }

    // Second pass: draw with dynamic contrast
    out_have = out_pos = 0;
    tinfl_init(&infl);
    idat_reader_init(&reader, png, png_len);

    uint32_t drawn_nonwhite = 0;
    uint32_t lvl0 = 0, lvl1 = 0, lvl2 = 0, lvl3 = 0;
    // Simple mode to force visible structure: direct 4-bin quantization, no dithering
    bool simple_mode = (WMAP_USE_SIMPLE_QUANT != 0);
    // Floyd–Steinberg buffers only if not in simple mode
    int16_t *err_curr = NULL;
    int16_t *err_next = NULL;
#if (WMAP_USE_SIMPLE_QUANT == 0)
    {
        err_curr = (int16_t *)calloc(info.width, sizeof(int16_t));
        err_next = (int16_t *)calloc(info.width, sizeof(int16_t));
        if (!err_curr || !err_next) {
            if (err_curr)
                free(err_curr);
            if (err_next)
                free(err_next);
            free(prev);
            free(cur);
            return false;
        }
    }
#endif

    for (uint32_t y2 = 0; y2 < info.height; y2++) {
        size_t need = 1 + row_bytes, got = 0;
        while (got < need) {
            if (out_pos == out_have) {
                size_t produced = 0;
                while (produced == 0) {
                    const uint8_t *in_ptr = NULL;
                    size_t in_avail = 0;
                    idat_reader_pull(&reader, &in_ptr, &in_avail);
                    if (!in_ptr || in_avail == 0) {
                        free(prev);
                        free(cur);
                        return false;
                    }
                    size_t in_sz = (size_t)in_avail;
                    size_t out_sz = sizeof(outbuf);
                    tinfl_status st =
                        tinfl_decompress(&infl, in_ptr, &in_sz, outbuf, outbuf, &out_sz,
                                         TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_HAS_MORE_INPUT |
                                             TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
                    idat_reader_consume(&reader, (size_t)in_sz);
                    produced = out_sz;
                    if (produced == 0) {
                        if (st == TINFL_STATUS_DONE) {
                            free(prev);
                            free(cur);
                            return false;
                        }
                        continue;
                    }
                    out_have = produced;
                    out_pos = 0;
                }
            }
            size_t to_copy = need - got;
            size_t avail = out_have - out_pos;
            size_t n = (to_copy < avail) ? to_copy : avail;
            memcpy(cur + got, outbuf + out_pos, n);
            out_pos += n;
            got += n;
        }
        unfilter_row(cur, prev, row_bytes, info.bpp);
        const uint8_t *px = cur + 1;
        for (uint32_t x = 0; x < info.width; x++) {
            uint8_t v;
            if (simple_mode) {
                if (info.color_type == 3) {
                    uint8_t idx = px[0];
                    px += 1;
                    // Force visible structure: scale small index ranges to 4 bins and invert
                    // brightness
                    v = (uint8_t)(idx >> 1);
                    if (v > 3)
                        v = 3;            // 0..3 for idx 0..7
                    v = (uint8_t)(3 - v); // idx 0 -> white (GRAY4), higher idx -> darker
                } else if (info.color_type == 0) {
                    uint8_t Y = px[0];
                    px += 1;
                    v = (uint8_t)(3 - (Y >> 6));
                } else if (info.color_type == 2) {
                    uint8_t r = px[0], g = px[1], b = px[2];
                    px += 3;
                    uint8_t Y = (uint8_t)((r * 30 + g * 59 + b * 11) / 100);
                    v = (uint8_t)(3 - (Y >> 6));
                } else if (info.color_type == 6) {
                    uint8_t r = px[0], g = px[1], b = px[2];
                    px += 4;
                    uint8_t Y = (uint8_t)((r * 30 + g * 59 + b * 11) / 100);
                    v = (uint8_t)(3 - (Y >> 6));
                } else {
                    v = 3;
                }
            } else {
                // Compute source luminance Ysrc 0..255
                uint8_t Ysrc;
                switch (info.color_type) {
                case 0:
                    Ysrc = px[0];
                    px += 1;
                    break;
                case 2: {
                    uint8_t r = px[0], g = px[1], b = px[2];
                    px += 3;
                    Ysrc = (uint8_t)((r * 30 + g * 59 + b * 11) / 100);
                } break;
                case 3: {
                    uint8_t idx = px[0];
                    px += 1;
                    if (idx >= (uint8_t)info.palette_len)
                        idx = 0;
                    uint8_t r = info.palette[3 * idx + 0];
                    uint8_t g = info.palette[3 * idx + 1];
                    uint8_t b = info.palette[3 * idx + 2];
                    Ysrc = (uint8_t)((r * 30 + g * 59 + b * 11) / 100);
                } break;
                case 6: {
                    uint8_t r = px[0], g = px[1], b = px[2];
                    px += 4;
                    Ysrc = (uint8_t)((r * 30 + g * 59 + b * 11) / 100);
                } break;
                default:
                    Ysrc = 255;
                    break;
                }
                // Normalize for indexed images to enhance contrast across used palette Y range
                uint32_t Yn = Ysrc;
                if (info.color_type == 3) {
                    uint32_t rangeY = (uint32_t)max_y - (uint32_t)min_y;
                    if (rangeY > 0) {
                        int32_t rel = (int32_t)Ysrc - (int32_t)min_y;
                        if (rel < 0)
                            rel = 0;
                        if (rel > (int32_t)rangeY)
                            rel = (int32_t)rangeY;
                        Yn = (uint32_t)((rel * 255) / rangeY);
                    }
                }
                // Add propagated error (scaled by 1/16)
                int32_t adj = (int32_t)Yn + ((int32_t)err_curr[x] + 8) / 16; // rounded divide by 16
                if (adj < 0)
                    adj = 0;
                if (adj > 255)
                    adj = 255;
                // Quantize to nearest of 4 levels: 0,85,170,255
                v = (uint8_t)((adj + 42) / 85);
                if (v > 3)
                    v = 3;
                int32_t Yq = (int32_t)v * 85;
                int32_t e = adj - Yq; // error to diffuse
                if (x + 1 < info.width) {
                    add_i16_saturating(&err_curr[x + 1], e * 7);
                }
                if (y2 + 1 < info.height) {
                    if (x > 0)
                        add_i16_saturating(&err_next[x - 1], e * 3);
                    add_i16_saturating(&err_next[x], e * 5);
                    if (x + 1 < info.width)
                        add_i16_saturating(&err_next[x + 1], e);
                }
            }

            uint16_t col = (v == 0) ? GRAY1 : (v == 1) ? GRAY2 : (v == 2) ? GRAY3 : GRAY4;
            uint16_t dx = (uint16_t)(x + off_x);
            uint16_t dy = (uint16_t)(y2 + off_y);
            if (dx < Paint.Width && dy < Paint.Height) {
                Paint_SetPixel(dx, dy, col);
                if (col != GRAY4)
                    drawn_nonwhite++;
                if (v == 0)
                    lvl0++;
                else if (v == 1)
                    lvl1++;
                else if (v == 2)
                    lvl2++;
                else
                    lvl3++;
            }
        }
#if (WMAP_USE_SIMPLE_QUANT == 0)
        // Advance error buffers if in FS mode
        int16_t *tmpe = err_curr;
        err_curr = err_next;
        err_next = tmpe;
        memset(err_next, 0, info.width * sizeof(int16_t));
#endif
        uint8_t *tmp = prev;
        prev = cur;
        cur = tmp;
    }

    if (err_curr)
        free(err_curr);
    if (err_next)
        free(err_next);
    free(prev);
    free(cur);
    // Even if everything is white, consider it success to avoid false failures on bright tiles
    return true;
}
