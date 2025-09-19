#include "weathermap_client.h"
#include "http_client.h"
#include "debug.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "lwip/ip_addr.h"
#include "flash.h"
#include "wifi.h"
#include "pico/cyw43_arch.h"
#include "third_party/GUI/GUI_Paint.h"
#include "fonts.h"
#include "png_stream.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>


#ifdef USE_CASE_WEATHERMAP

// Host and IP for sgx.geodatenzentrum.de (DNS disabled, using known IP)
#define WEATHERMAP_HOST   "sgx.geodatenzentrum.de"
#define WEATHERMAP_IP0    141
#define WEATHERMAP_IP1    74
#define WEATHERMAP_IP2    64
#define WEATHERMAP_IP3    40
#define WEATHERMAP_PORT   443

// Dynamic WMS path centered on Dibbesdorf (Braunschweig) with 4:3 window
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define WMAP_CENTER_LAT   (52.302373)   // New center
#define WMAP_CENTER_LON   (10.6021966)
#define WMAP_HALF_WIDTH_M   (20000.0)   // Base: 40 km total horizontal span (adjust height by aspect)

// Resolve target PNG size from configured ePaper type
static inline void wmap_get_target_size(int* out_w, int* out_h) {
    int w = 400, h = 300; // default to 4.2" panel
    switch (device_config_flash.data.epapertype) {
        case EPAPER_WAVESHARE_7IN5_V2: w = 800; h = 480; break;
        case EPAPER_WAVESHARE_4IN2_V2: w = 400; h = 300; break;
        default: break;
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

static void wmap_center_to_bbox(double lat_deg, double lon_deg,
                                double half_w_m, double half_h_m,
                                double* xmin, double* ymin, double* xmax, double* ymax) {
    const double R = 6378137.0;
    const double lat = lat_deg * (M_PI / 180.0);
    const double lon = lon_deg * (M_PI / 180.0);
    const double x = R * lon;
    const double y = R * log(tan(M_PI/4.0 + lat/2.0));
    *xmin = x - half_w_m; *xmax = x + half_w_m;
    *ymin = y - half_h_m; *ymax = y + half_h_m;
}

static int wmap_build_http_get(char* dst, size_t dst_len) {
    double xmin, ymin, xmax, ymax;
    int tw, th; wmap_get_target_size(&tw, &th);
    // Adjust vertical half-size to match target aspect
    double half_h_m = WMAP_HALF_WIDTH_M * ((double)th / (double)tw);
    wmap_center_to_bbox(WMAP_CENTER_LAT, WMAP_CENTER_LON,
                        WMAP_HALF_WIDTH_M, half_h_m,
                        &xmin, &ymin, &xmax, &ymax);
    char path[512];
    int pn = snprintf(path, sizeof(path),
        "/wms_basemapde?service=WMS&version=1.1.1&request=GetMap&"
        "layers=de_basemapde_web_raster_grau&styles=default&format=image/png8&"
        "srs=EPSG:3857&bbox=%.1f,%.1f,%.1f,%.1f&width=%d&height=%d",
        xmin, ymin, xmax, ymax, tw, th);
    if (pn <= 0 || (size_t)pn >= sizeof(path)) return -1;
    int n = snprintf(dst, dst_len,
        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: inki/weathermap\r\nConnection: close\r\n\r\n",
        path, WEATHERMAP_HOST);
    return (n > 0 && (size_t)n < dst_len) ? n : -1;
}

typedef struct {
    uint8_t* buf;
    size_t   max_len;
    size_t   out_len;
    bool     complete;
    bool     success;
} fetch_ctx_t;

static void fetch_complete_cb(const char* body, size_t length, bool success, void* arg) {
    fetch_ctx_t* ctx = (fetch_ctx_t*)arg;
    ctx->success = success && (body != NULL) && (length > 0);
    if (ctx->success) {
        if (length > ctx->max_len) {
            length = ctx->max_len; // clamp
        }
        memcpy(ctx->buf, body, length);
        ctx->out_len = length;
    }
    ctx->complete = true;
}

typedef struct {
    size_t out_len;
    bool   success;
    bool   complete;
} inline_ctx_t;

static void inline_complete_cb(const char* body, size_t length, bool success, void* arg) {
    inline_ctx_t* ictx = (inline_ctx_t*)arg;
    if (success && body && length > 0) {
        ictx->success = weathermap_process_png_and_store((const uint8_t*)body, length);
        ictx->out_len = length;
    } else {
        ictx->success = false;
        ictx->out_len = 0;
    }
    ictx->complete = true;
}

bool weathermap_fetch_png(uint8_t* out_buf, size_t max_len, size_t* out_len) {
    if (!out_buf || max_len == 0 || !out_len) return false;

    // Build simple HTTP/1.0 GET with Host header; TLS will be enabled by port==443
    char req[512];
    int n = wmap_build_http_get(req, sizeof(req));
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        debug_log_with_color(COLOR_RED, "[WEATHERMAP] Request build failed\n");
        return false;
    }

    ip_addr_t ip;
    IP4_ADDR(&ip, WEATHERMAP_IP0, WEATHERMAP_IP1, WEATHERMAP_IP2, WEATHERMAP_IP3);

    fetch_ctx_t ctx = {
        .buf = out_buf,
        .max_len = max_len,
        .out_len = 0,
        .complete = false,
        .success = false
    };

    http_result_t r = http_request_async(&ip, WEATHERMAP_PORT, req, fetch_complete_cb, &ctx);
    if (r != HTTP_SUCCESS) {
        debug_log_with_color(COLOR_RED, "[WEATHERMAP] http_request_async failed: %d\n", r);
        return false;
    }

    // Wait for completion with a timeout budget - curl completes in ~1s
    int waits = 0;
    const int max_waits = 500; // 500 * 10ms = 5s (should be enough based on curl)
    while (!ctx.complete && waits < max_waits) {
        sleep_ms(10);
        watchdog_update();
        waits++;
        
        // Log progress every 1 second
        if (waits % 100 == 0) {
            debug_log("[WEATHERMAP] Still waiting... (%ds elapsed)\n", waits / 100);
        }
    }

    if (!ctx.complete || !ctx.success) {
        debug_log_with_color(COLOR_RED, "[WEATHERMAP] Fetch failed or timed out\n");
        return false;
    }

    *out_len = ctx.out_len;
    return true;
}

// Streaming stage context
typedef struct {
    volatile bool complete;
    volatile bool success;
    size_t total;
    bool is_bmp;
} stage_ctx_t;

static void stream_header_cb(const char* header, size_t header_len, int status_code, int content_length, void* arg) {
    stage_ctx_t* sctx = (stage_ctx_t*)arg;
    debug_log("[WMAP] HTTP status: %d, Content-Length: %d\n", status_code, content_length);
    if (status_code >= 200 && status_code < 300) {
        wmap_staging_begin();
        sctx->success = true;
    } else {
        sctx->success = false;
    }
    // Detect BMP via Content-Type header (if present)
    sctx->is_bmp = false;
    if (header && header_len > 0) {
        if (strstr(header, "Content-Type:") && (strstr(header, "image/bmp") || strstr(header, "image/x-bmp"))) {
            sctx->is_bmp = true;
        }
    }
}

static void stream_data_cb(const uint8_t* data, size_t len, void* arg) {
    stage_ctx_t* sctx = (stage_ctx_t*)arg;
    if (!sctx->success) return; // ignore if already failed
    if (!wmap_staging_append(data, len)) {
        sctx->success = false;
    } else {
        sctx->total += len;
        if ((sctx->total & 0x7FFF) == 0) { // log roughly every 32 KB boundary
            debug_log("[WMAP] Streamed %u bytes...\n", (unsigned)sctx->total);
        }
    }
}

static void stream_complete_cb(bool success, void* arg) {
    stage_ctx_t* sctx = (stage_ctx_t*)arg;
    if (success && sctx->success) {
        uint32_t bytes = 0;
        if (wmap_staging_end(&bytes)) {
            set_weathermap_meta(bytes);
            sctx->total = bytes;
            sctx->success = true;
            (void)wmap_staging_ptr();
        } else {
            sctx->success = false;
        }
    } else {
        wmap_staging_abort();
        sctx->success = false;
    }
    sctx->complete = true;
}

static bool weathermap_fetch_and_store_inline(size_t* out_len) {
    // Build request
    char req[512];
    int n = wmap_build_http_get(req, sizeof(req));
    if (n <= 0 || (size_t)n >= sizeof(req)) return false;
    ip_addr_t ip; IP4_ADDR(&ip, WEATHERMAP_IP0, WEATHERMAP_IP1, WEATHERMAP_IP2, WEATHERMAP_IP3);
    stage_ctx_t sctx = { .complete = false, .success = false, .total = 0, .is_bmp = false };
    http_result_t r = http_request_async_stream(&ip, WEATHERMAP_PORT, req,
                                                stream_header_cb,
                                                stream_data_cb,
                                                stream_complete_cb,
                                                &sctx);
    if (r != HTTP_SUCCESS) return false;
    int waits = 0; const int max_waits = 1000; // up to 10s
    while (!sctx.complete && waits < max_waits) { sleep_ms(10); watchdog_update(); waits++; }
    if (out_len) *out_len = sctx.total;
    return sctx.complete && sctx.success;
}

typedef struct {
    size_t out_len;
    bool complete;
    bool success;
} count_ctx_t;

static void count_complete_cb(const char* body, size_t length, bool success, void* arg) {
    count_ctx_t* ctx = (count_ctx_t*)arg;
    ctx->success = success;
    ctx->out_len = length;
    ctx->complete = true;
}

bool weathermap_fetch_count(size_t* out_len) {
    if (!out_len) return false;
    char req[512];
    int n = wmap_build_http_get(req, sizeof(req));
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        return false;
    }
    ip_addr_t ip;
    IP4_ADDR(&ip, WEATHERMAP_IP0, WEATHERMAP_IP1, WEATHERMAP_IP2, WEATHERMAP_IP3);
    count_ctx_t ctx = { .out_len = 0, .complete = false, .success = false };
    http_result_t r = http_request_async_count_only(&ip, WEATHERMAP_PORT, req, count_complete_cb, &ctx);
    if (r != HTTP_SUCCESS) return false;
    int waits = 0;
    const int max_waits = 500; // 5s should be enough based on curl
    while (!ctx.complete && waits < max_waits) { 
        sleep_ms(10); 
        watchdog_update(); 
        waits++;
        if (waits % 100 == 0) {
            debug_log("[WEATHERMAP] Count fetch still waiting... (%ds elapsed)\n", waits / 100);
        }
    }
    if (!ctx.complete || !ctx.success) return false;
    *out_len = ctx.out_len;
    return true;
}

// --- PNG processing to 2-bit and flash store (streaming)
bool weathermap_process_png_and_store(const uint8_t* png_data, size_t png_len) {
    // Stream decode PNG (inflate + unfilter) and write line-by-line into slot1 image area
    return png_stream_decode_to_flash_from_xip(png_data, png_len);
}

bool weathermap_render_from_flash(void) {
    // Decode staged PNG in slot1 and draw directly into Paint at (10,10)
    uint32_t staged_bytes = 0;
    if (!get_weathermap_meta(&staged_bytes) || staged_bytes < 64) {
        debug_log_with_color(COLOR_YELLOW, "[WEATHERMAP] No staged PNG marker; cannot draw.\n");
        return false;
    }
    const uint8_t* png = (const uint8_t*)FLASH_PTR(FIRMWARE_SLOT1_FLASH_OFFSET);
    bool ok = png_stream_draw_to_paint_from_xip(png, (size_t)staged_bytes, 0, 0);
    if (ok) {
        // Draw a simple scale bar at bottom-left: length = 1/10 of WMS width (in pixels)
        // and label with real-world length (based on bbox width in meters).
        const int margin = 6;
        const int shift_x = 20; // shift scale bar to the right
        const int bar_h = 6;
        const int scale_px = Paint.Width / 10; // scale bar is 1/10th of current canvas width
        int x0 = margin + shift_x;
        int y1 = Paint.Height - margin;
        int y0 = y1 - bar_h;
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
        int x1 = x0 + scale_px;
        // Draw filled black rectangle as scale bar
        Paint_DrawRectangle((UWORD)x0, (UWORD)y0, (UWORD)x1, (UWORD)y1, GRAY1, DOT_PIXEL_1X1, DRAW_FILL_FULL);

        // Build label text for physical length (meters or km)
        char label[32];
        double meters = (2.0 * WMAP_HALF_WIDTH_M) / 10.0;
        if (meters >= 1000.0) {
            double km = meters / 1000.0;
            // Use 0 decimals if close to integer, else 1 decimal
            if (fabs(km - (int)km) < 0.05) {
                snprintf(label, sizeof(label), "%d km", (int)km);
            } else {
                snprintf(label, sizeof(label), "%.1f km", km);
            }
        } else {
            // Round to nearest 10 m for neatness
            int m10 = (int)((meters + 5.0) / 10.0) * 10;
            snprintf(label, sizeof(label), "%d m", m10);
        }
        // Place label just above the bar, left-aligned
        const sFONT* f = &Font12;
        // Increase spacing between text and scale by 5px (was 2px)
        const int label_gap = 7;
        int ty = y0 - (int)f->Height - label_gap;
        if (ty < 0) ty = y1 + label_gap; // fallback below the bar if not enough room above

        // Draw a white rectangle slightly larger than the text box (4 px larger total: 2 px padding each side)
        int text_w = (int)strlen(label) * (int)f->Width;
        int text_h = (int)f->Height;
        int pad = 2;
        int rx0 = x0 - pad;
        int ry0 = ty - pad;
        int rx1 = x0 + text_w + pad;
        int ry1 = ty + text_h + pad;
        if (rx0 < 0) rx0 = 0;
        if (ry0 < 0) ry0 = 0;
        if (rx1 > (int)Paint.Width) rx1 = (int)Paint.Width;
        if (ry1 > (int)Paint.Height) ry1 = (int)Paint.Height;
        Paint_DrawRectangle((UWORD)rx0, (UWORD)ry0, (UWORD)rx1, (UWORD)ry1, GRAY4, DOT_PIXEL_1X1, DRAW_FILL_FULL);

        // Draw black text on white background inside the rectangle.
        // Note: Paint_DrawString_EN swaps its color args when calling Paint_DrawChar, so pass reversed here.
        Paint_DrawString_EN((UWORD)x0, (UWORD)ty, label, (sFONT*)f, GRAY4, GRAY1);
    }
    if (!ok) {
        debug_log_with_color(COLOR_RED, "[WEATHERMAP] png_stream_draw_to_paint failed.\n");
    }
    return ok;
}

void weathermap_boot_fetch_if_needed(void) {
    // Ensure HTTP client/TLS is initialized
    extern bool http_client_init(void);
    http_client_init();

    // Always refetch on boot (overwrite any previous image)
    debug_log_with_color(COLOR_BOLD_YELLOW, "[WEATHERMAP] Always refetch enabled — fetching on boot.\n");

    // Connect Wi‑Fi (STA)
    WifiResult w = wifi_connect();
    if (w != WIFI_SUCCESS) {
        debug_log_with_color(COLOR_RED, "[WEATHERMAP] Wi‑Fi connect failed: %d\n", w);
        return;
    }

    // Now create TLS config after Wi-Fi is connected (pico-examples pattern)
    extern void tls_create_config_after_wifi(void);
    tls_create_config_after_wifi();

    // Build and log request for transparency (static path)
    char req[512];
    int n = wmap_build_http_get(req, sizeof(req));
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        debug_log_with_color(COLOR_RED, "[WEATHERMAP] Request build failed\n");
        cyw43_arch_deinit();
        return;
    }

    debug_log("[WEATHERMAP] HTTPS request (port 443) to %s (%d.%d.%d.%d):\n%.*s\n",
              WEATHERMAP_HOST, WEATHERMAP_IP0, WEATHERMAP_IP1, WEATHERMAP_IP2, WEATHERMAP_IP3, n, req);

    // Fetch PNG via streaming into slot1
    size_t count = 0;
    bool ok = weathermap_fetch_and_store_inline(&count);
    if (!ok) {
        debug_log_with_color(COLOR_RED, "[WEATHERMAP] Fetch failed\n");
        wifi_log_rssi();
        cyw43_arch_deinit();
        return;
    }

    debug_log_with_color(COLOR_GREEN, "[WEATHERMAP] TLS fetch OK, bytes=%u\n", (unsigned)count);
    set_weathermap_meta((uint32_t)count);
    wifi_log_rssi();
    cyw43_arch_deinit();

    // Do not decode to flash here. We will decode and draw directly from slot1 at render time.
    const uint8_t* data = wmap_staging_ptr();
    size_t data_len = wmap_staging_size();
    if (!(data && data_len >= 8 && data[0]==0x89 && data[1]=='P' && data[2]=='N' && data[3]=='G')) {
        debug_log_with_color(COLOR_YELLOW, "[WMAP] Staged content missing/invalid PNG signature.\n");
    }
}
#endif // USE_CASE_WEATHERMAP
