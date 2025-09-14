#include "weathermap_client.h"
#include "http_client.h"
#include "debug.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "lwip/ip_addr.h"
#include "flash.h"
#include "wifi.h"
#include "pico/cyw43_arch.h"

#include <string.h>

#ifdef USE_CASE_WEATHERMAP

// Host and IP for sgx.geodatenzentrum.de (DNS disabled, using known IP)
#define WEATHERMAP_HOST   "sgx.geodatenzentrum.de"
#define WEATHERMAP_IP0    141
#define WEATHERMAP_IP1    74
#define WEATHERMAP_IP2    64
#define WEATHERMAP_IP3    40
#define WEATHERMAP_PORT   443

// Example WMS path + query for gray basemap
// Keep dimensions moderate to fit RAM (TLS + buffers). Start with 512x512.
static const char* WEATHERMAP_PATH = \
    "/wms_basemapde?service=WMS&version=1.1.1&request=GetMap&" \
    "layers=de_basemapde_web_raster_grau&styles=default&format=image/png&" \
    "srs=EPSG:3857&bbox=1170543.2,6844879.8,1190543.2,6864879.8&width=512&height=512";

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

bool weathermap_fetch_png(uint8_t* out_buf, size_t max_len, size_t* out_len) {
    if (!out_buf || max_len == 0 || !out_len) return false;

    // Build simple HTTP/1.0 GET with Host header; TLS will be enabled by port==443
    char req[512];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.0\r\n"
                     "Host: %s\r\n"
                     "User-Agent: inki/weathermap\r\n"
                     "Connection: close\r\n\r\n",
                     WEATHERMAP_PATH, WEATHERMAP_HOST);
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

#endif // USE_CASE_WEATHERMAP

#ifdef USE_CASE_WEATHERMAP
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
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.0\r\n"
                     "Host: %s\r\n"
                     "User-Agent: inki/weathermap\r\n"
                     "Connection: close\r\n\r\n",
                     WEATHERMAP_PATH, WEATHERMAP_HOST);
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

void weathermap_boot_fetch_if_needed(void) {
    // Ensure HTTP client/TLS is initialized
    extern bool http_client_init(void);
    http_client_init();
    uint32_t bytes = 0;
    if (get_weathermap_meta(&bytes)) {
        debug_log_with_color(COLOR_YELLOW, "[WEATHERMAP] Map marker present: %u bytes. Skipping fetch.\n", (unsigned)bytes);
        return;
    }

    debug_log_with_color(COLOR_BOLD_YELLOW, "[WEATHERMAP] No map marker – attempting fetch on boot.\n");

    // Connect Wi‑Fi (STA)
    WifiResult w = wifi_connect();
    if (w != WIFI_SUCCESS) {
        debug_log_with_color(COLOR_RED, "[WEATHERMAP] Wi‑Fi connect failed: %d\n", w);
        return;
    }

    // Now create TLS config after Wi-Fi is connected (pico-examples pattern)
    extern void tls_create_config_after_wifi(void);
    tls_create_config_after_wifi();

    // Build and log request for transparency
    char req[512];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: inki/weathermap\r\nConnection: close\r\n\r\n",
                     WEATHERMAP_PATH, WEATHERMAP_HOST);
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        debug_log_with_color(COLOR_RED, "[WEATHERMAP] Request build failed\n");
        cyw43_arch_deinit();
        return;
    }

    debug_log("[WEATHERMAP] HTTPS request (port 443) to %s (%d.%d.%d.%d):\n%.*s\n",
              WEATHERMAP_HOST, WEATHERMAP_IP0, WEATHERMAP_IP1, WEATHERMAP_IP2, WEATHERMAP_IP3, n, req);

    size_t count = 0;
    bool ok = weathermap_fetch_count(&count);
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
}
#endif // USE_CASE_WEATHERMAP
