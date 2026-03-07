/*
 * ==============================================================================
 * HTTP Processing Overview – inki Webserver
 * ==============================================================================
 *
 * Connection ──► recv_cb()
 *              │
 *              ├─ Collect HTTP header into upload_session.header_buffer
 *              │
 *              ├─ Once \r\n\r\n is found:
 *              │    └─ header_complete = true
 *              │
 *              └─ Analyze first request line:
 *                   - GET /...    → send_..._page(tpcb)
 *                   - POST /...   → handle_post_...(tpcb, ...)
 *                                  │
 *                                  ├─ Form uploads:
 *                                  │    ├─ Copy body into form_buffer (RAM)
 *                                  │    └─ Call handle_form_...(tpcb, buffer)
 *                                  │
 *                                  └─ Binary uploads (logo / firmware):
 *                                       ├─ Erase flash sector
 *                                       ├─ Write body chunks:
 *                                       │     - Fill flash_writer.buffer
 *                                       │     - On 4096 bytes: flush_page_to_flash()
 *                                       └─ On completion:
 *                                             - Flush last page
 *                                             - Send "Upload OK" page
 *
 * Responses:
 *  send_response(tpcb, body) → send_next_chunk() → tcp_write() in 1024-byte chunks
 *
 * Data structures:
 *  - upload_session_t
 *      .header_buffer[2048], .form_buffer[32k]
 *      .active, .header_complete, .expected_length, .total_received, .type
 *
 *  - flash_writer
 *      .buffer[4096], .buffer_filled, .flash_offset
 *
 * GET routes:
 *  /                 → Landing page
 *  /wifi             → Wi-Fi config
 *  /seatsurfing      → Seatsurfing API settings
 *  /device_config    → Device configuration
 *  /clock            → Set RTC time
 *  /device_status    → System status
 *  /upload_logo      → Logo upload page
 *  /firmware_update  → Firmware update page
 *  /shutdown         → Trigger RTC-based shutdown
 *
 * POST routes:
 *  /wifi             → handle_post_wifi()
 *  /seatsurfing      → handle_post_seatsurfing()
 *  /device_config    → handle_post_device_config()
 *  /clock            → handle_post_clock()
 *  /upload_logo      → handle_post_upload_logo()
 *  /firmware_update  → handle_post_firmware_update()
 *  /delete_logo      → Immediate flash erase
 *
 * Note:
 *  Large uploads (firmware, logo) are streamed directly into flash memory
 *  → no full buffering in RAM required.
 *
 * ==============================================================================
 */

#include "webserver.h"
#include "debug.h"
#include "ds3231.h"
#include "flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "lwip/tcp.h"
#include "main.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/time.h"
#include "webserver_pages.h"
#include "webserver_utils.h"
#include "wifi.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef USE_CASE_WEATHERMAP
// Ensure prototype visible when building for WEATHERMAP
void send_weathermap_page(struct tcp_pcb *tpcb, const char *message);
#endif
#ifdef USE_CASE_WEATHERMAP
#include "weathermap_client.h"
#endif

#include <stddef.h>
#include <stdint.h>

#define TCP_CHUNK_SIZE 1024

// Global upload session instance
upload_session_t upload_session = {0};
static int g_firmware_progress_logged = -10;

bool webserver_upload_in_progress(void) { return upload_session.active; }

bool webserver_firmware_upload_active(void) {
    return upload_session.active && upload_session.type == UPLOAD_FIRMWARE;
}

// =============================================================================
// ROUTE TABLE STRUCTURES & TYPES
// =============================================================================

// Route table structures
typedef enum { HTTP_GET, HTTP_POST } http_method_t;

typedef enum {
    ROUTE_SIMPLE, // Standard page handler (GET only)
    ROUTE_FORM,   // Form handler (POST only)
    ROUTE_BINARY, // Binary upload handler (POST only)
    ROUTE_INLINE  // Inline logic handler
} route_type_t;

typedef struct {
    const char *path;
    http_method_t method;
    route_type_t type;
    union {
        void (*simple_handler)(struct tcp_pcb *tpcb);
        void (*form_handler)(struct tcp_pcb *tpcb, const char *body, int len);
        void (*binary_handler)(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer, int len);
        void (*inline_handler)(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer, int len);
    } handler;
} route_t;

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

// Forward declarations for route handlers
static void handle_shutdown_route(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                                  int len);
static void handle_delete_logo_route(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                                     int len);
static void handle_logo_route(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer, int len);
static void handle_firmware_demote_active_route(struct tcp_pcb *tpcb, struct pbuf *p,
                                                const char *buffer, int len);
// Binary streaming helper (implemented later)
static void send_binary_response(struct tcp_pcb *tpcb, const char *content_type,
                                 const uint8_t *data, size_t len);
static int64_t shutdown_callback(alarm_id_t id, void *user_data);

// Forward declarations for flash functions
static void mark_firmware_valid(uint32_t flash_offset);
static bool demote_firmware_header_priority(uint32_t header_flash_offset);
static void flush_page_to_flash(void);

static const char *use_case_name_from_id(uint8_t use_case_id) {
    switch (use_case_id) {
    case USE_CASE_ID_SEATSURFING:
        return "SeatSurfing";
    case USE_CASE_ID_HISTORIAN:
        return "Historian";
    case USE_CASE_ID_HOMEMATIC:
        return "Homematic";
    case USE_CASE_ID_WEATHERMAP:
        return "Weathermap";
    case USE_CASE_ID_NEW_USECASE:
        return "NewUseCase";
    default:
        return NULL;
    }
}

static bool firmware_header_use_case_meta_valid(const firmware_header_t *header) {
    if (!header) {
        return false;
    }

    if (header->meta_version != 1) {
        return false;
    }

    const char *expected_name = use_case_name_from_id(header->use_case_id);
    if (!expected_name) {
        return false;
    }

    size_t name_len = 0;
    while (name_len < sizeof(header->use_case_name) && header->use_case_name[name_len] != '\0') {
        name_len++;
    }

    if (name_len == 0 || name_len == sizeof(header->use_case_name)) {
        return false;
    }

    return strcmp(header->use_case_name, expected_name) == 0;
}

static bool zero_active_use_case_settings_sector(void) {
#if defined(USE_CASE_SEATSURFING)
    const uint32_t cfg_offset = SEATSURFING_CONFIG_FLASH_OFFSET;
#elif defined(USE_CASE_HISTORIAN)
    const uint32_t cfg_offset = HISTORIAN_CONFIG_FLASH_OFFSET;
#elif defined(USE_CASE_HOMEMATIC)
    const uint32_t cfg_offset = HOMEMATIC_CONFIG_FLASH_OFFSET;
#elif defined(USE_CASE_WEATHERMAP)
    const uint32_t cfg_offset = WEATHERMAP_CONFIG_FLASH_OFFSET;
#else
    const uint32_t cfg_offset = (CONFIG_FLASH_OFFSET + 0x1000);
#endif

    const uint32_t sector_offset = cfg_offset & ~(FLASH_SECTOR_SIZE - 1);
    uint8_t zero_page[FLASH_PAGE_SIZE];
    memset(zero_page, 0x00, sizeof(zero_page));

    debug_log_with_color(COLOR_YELLOW,
                         "FIRMWARE: Use-case mismatch -> zeroing settings sector at 0x%X\n",
                         sector_offset);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_offset, FLASH_SECTOR_SIZE);
    for (uint32_t off = 0; off < FLASH_SECTOR_SIZE; off += FLASH_PAGE_SIZE) {
        flash_range_program(sector_offset + off, zero_page, FLASH_PAGE_SIZE);
    }
    restore_interrupts(ints);

    debug_log_with_color(COLOR_YELLOW, "FIRMWARE: Settings sector zeroed.\n");
    return true;
}

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================
//
// For new GET routes: Add wrapper function here if page handler needs parameters
// Pattern: static void send_xxx_page_wrapper(struct tcp_pcb *tpcb) { send_xxx_page(tpcb, ""); }

// Wrapper functions for page handlers that take parameters
static void send_wifi_config_page_wrapper(struct tcp_pcb *tpcb) { send_wifi_config_page(tpcb, ""); }

#ifdef USE_CASE_SEATSURFING
static void send_seatsurfing_config_page_wrapper(struct tcp_pcb *tpcb) {
    send_seatsurfing_config_page(tpcb, "");
}
#elif defined(USE_CASE_HISTORIAN)
static void send_historian_config_page_wrapper(struct tcp_pcb *tpcb) {
    send_historian_config_page(tpcb, "");
}
#elif defined(USE_CASE_HOMEMATIC)
static void send_homematic_config_page_wrapper(struct tcp_pcb *tpcb) {
    send_homematic_config_page(tpcb, "");
}
#endif

static void send_device_config_page_wrapper(struct tcp_pcb *tpcb) {
    send_device_config_page(tpcb, "");
}

static void send_clock_page_wrapper(struct tcp_pcb *tpcb) { send_clock_page(tpcb, ""); }

static void send_upload_logo_page_wrapper(struct tcp_pcb *tpcb) { send_upload_logo_page(tpcb, ""); }

static void send_firmware_update_page_wrapper(struct tcp_pcb *tpcb) {
    send_firmware_update_page(tpcb, "");
}
static void send_settings_transfer_page_wrapper(struct tcp_pcb *tpcb) {
    send_settings_transfer_page(tpcb, "");
}
static void send_settings_export_txt_wrapper(struct tcp_pcb *tpcb) {
    send_settings_export_txt(tpcb);
}
static void send_message_page_wrapper(struct tcp_pcb *tpcb) { send_message_page(tpcb, ""); }

#ifdef USE_CASE_WEATHERMAP
static void send_weathermap_page_wrapper(struct tcp_pcb *tpcb) { send_weathermap_page(tpcb, ""); }
#endif

// Helper function for consistent cleanup
static void cleanup_and_return(struct tcp_pcb *tpcb, struct pbuf *p, int copied) {
    tcp_recved(tpcb, copied);
    upload_session.header_complete = false;
    upload_session.header_length = 0;
    pbuf_free(p);
}

// Helper function for binary upload chunked processing
static void process_binary_upload_chunk(const char *buffer, int copied, const char *debug_prefix) {
    size_t to_copy = copied;
    const uint8_t *src = (const uint8_t *)buffer;

    while (to_copy > 0) {
        size_t space = FLASH_PAGE_SIZE - flash_writer.buffer_filled;
        size_t chunk = (to_copy < space) ? to_copy : space;

        memcpy(flash_writer.buffer + flash_writer.buffer_filled, src, chunk);
        flash_writer.buffer_filled += chunk;
        src += chunk;
        to_copy -= chunk;

        if (flash_writer.buffer_filled == FLASH_PAGE_SIZE) {
            flush_page_to_flash();
        }
    }
    upload_session.total_received += copied;
}

// Helper function for form upload chunked processing
static bool process_form_upload_chunk(const char *buffer, int copied, struct tcp_pcb *tpcb) {
    size_t to_copy = copied;
    if (upload_session.total_received + to_copy > upload_session.expected_length) {
        to_copy = upload_session.expected_length - upload_session.total_received;
    }

    memcpy(upload_session.form_buffer + upload_session.total_received, buffer, to_copy);
    upload_session.total_received += to_copy;
    tcp_recved(tpcb, copied);

    // Check if form is complete
    if (upload_session.total_received >= upload_session.expected_length) {
        upload_session.form_buffer[upload_session.expected_length] = '\0';

        // Dispatch to appropriate form handler
        switch (upload_session.type) {
        case UPLOAD_FORM_WIFI:
            handle_form_wifi(tpcb, upload_session.form_buffer, upload_session.expected_length);
            break;
#ifdef USE_CASE_SEATSURFING
        case UPLOAD_FORM_SEATSURFING:
            handle_form_seatsurfing(tpcb, upload_session.form_buffer,
                                    upload_session.expected_length);
            break;
#elif defined(USE_CASE_HISTORIAN)
        case UPLOAD_FORM_HISTORIAN:
            handle_form_historian(tpcb, upload_session.form_buffer, upload_session.expected_length);
            break;
#elif defined(USE_CASE_HOMEMATIC)
        case UPLOAD_FORM_HOMEMATIC:
            handle_form_homematic(tpcb, upload_session.form_buffer, upload_session.expected_length);
            break;
#elif defined(USE_CASE_WEATHERMAP)
        case UPLOAD_FORM_WEATHERMAP:
            handle_form_weathermap(tpcb, upload_session.form_buffer,
                                   upload_session.expected_length);
            break;
#endif
        case UPLOAD_FORM_DEVICE:
            handle_form_device_config(tpcb, upload_session.form_buffer,
                                      upload_session.expected_length);
            break;
        case UPLOAD_FORM_CLOCK:
            handle_form_clock(tpcb, upload_session.form_buffer, upload_session.expected_length);
            break;
        case UPLOAD_FORM_SETTINGS_IMPORT:
            handle_form_settings_import(tpcb, upload_session.form_buffer,
                                        upload_session.expected_length);
            break;
        default:
            debug_log_with_color(COLOR_RED, "Unknown form type: %d\n", upload_session.type);
            break;
        }

        // Reset upload session
        upload_session.active = false;
        upload_session.header_complete = false;
        upload_session.header_length = 0;
        return true; // Form complete
    }
    return false; // Form still in progress
}

// =============================================================================
// INLINE ROUTE HANDLERS
// =============================================================================

// Inline route handlers for special cases
static void handle_shutdown_route(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                                  int len) {
    static bool shutdown_triggered = false;
    if (shutdown_triggered) {
        debug_log("Shutdown already in progress, ignoring\n");
        return;
    }

    shutdown_triggered = true;
    debug_log("GET /shutdown called - redirecting and shutting down\n");

    send_response(tpcb, "<!DOCTYPE html><html><head>"
                        "<meta charset=\"UTF-8\">"
                        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                        "<title>Rebooting</title>"
                        "<style>"
                        "body { font-family: sans-serif; text-align: center; padding: 2em; }"
                        "h1 { font-size: 1.5em; color: #333; }"
                        "p { font-size: 1em; color: green; }"
                        "</style></head><body>"
                        "<h1>✔️ Rebooting...</h1>"
                        "</body></html>");

    tcp_output(tpcb);
    add_alarm_in_ms(600, shutdown_callback, NULL, false);
}

static void handle_delete_logo_route(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                                     int len) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(LOGO_FLASH_OFFSET, LOGO_FLASH_SIZE);
    restore_interrupts(ints);

    debug_log("UPLOAD: flash erased at address: %d , %d bytes.\n", LOGO_FLASH_OFFSET,
              LOGO_FLASH_SIZE);

    send_upload_logo_page(
        tpcb, "<p style='color:orange; font-weight:bold;'>✔️ Logo successfully deleted.</p>");
    upload_session.active = false;
    upload_session.header_complete = false;
    upload_session.header_length = 0;
}

static void handle_firmware_demote_active_route(struct tcp_pcb *tpcb, struct pbuf *p,
                                                const char *buffer, int len) {
    (void)p;
    (void)buffer;
    (void)len;

    const char *slot_info = get_active_firmware_slot_info();
    uint32_t active_offset = 0;
    uint8_t active_slot = 255;

    if (strncmp(slot_info, "SLOT_0", 6) == 0) {
        active_offset = FIRMWARE_SLOT0_FLASH_OFFSET;
        active_slot = 0;
    } else if (strncmp(slot_info, "SLOT_1", 6) == 0) {
        active_offset = FIRMWARE_SLOT1_FLASH_OFFSET;
        active_slot = 1;
    } else {
        send_firmware_update_page(tpcb, "<p style='color:red'><b>❌ Active firmware is not running "
                                        "from SLOT_0/SLOT_1.</b></p>");
        return;
    }

    if (!demote_firmware_header_priority(active_offset)) {
        send_firmware_update_page(tpcb, "<p style='color:red'><b>❌ Failed to demote active slot "
                                        "metadata (invalid header).</b></p>");
        return;
    }

    char msg[640];
    snprintf(
        msg, sizeof(msg),
        "<p style='color:orange'><b>⚠️ Active slot %u metadata demoted (developer helper).</b></p>"
        "<p>The current firmware keeps running. On the next reboot, the other valid slot "
        "should be preferred if it has a newer version.</p>"
        "<p>Rewritten fields in the active slot header: "
        "<code>git_version = v0.0.0-0</code>, <code>build_date = 1970-01-01</code>.</p>",
        (unsigned)active_slot);
    send_firmware_update_page(tpcb, msg);
}

static void handle_logo_route(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer, int len) {
    const logo_header_t *hdr = (const logo_header_t *)FLASH_PTR(LOGO_FLASH_OFFSET);
    if (memcmp(hdr->magic, "LOGO", 4) != 0 || hdr->width == 0 || hdr->height == 0) {
        // No logo in flash – return tiny 1x1 white BMP as placeholder
        static const unsigned char bmp1x1[] = {0x42, 0x4D,             // 'BM'
                                               0x3E, 0x00, 0x00, 0x00, // file size 62
                                               0x00, 0x00, 0x00, 0x00, // reserved
                                               0x3E, 0x00, 0x00, 0x00, // offset to pixel array (62)
                                               0x28, 0x00, 0x00, 0x00, // DIB header size (40)
                                               0x01, 0x00, 0x00, 0x00, // width 1
                                               0x01, 0x00, 0x00, 0x00, // height 1
                                               0x01, 0x00,             // planes 1
                                               0x01, 0x00,             // bpp 1
                                               0x00, 0x00, 0x00, 0x00, // compression BI_RGB
                                               0x04, 0x00, 0x00,
                                               0x00, // image size (4 bytes row padded)
                                               0x13, 0x0B, 0x00, 0x00, // ppm X
                                               0x13, 0x0B, 0x00, 0x00, // ppm Y
                                               0x00, 0x00, 0x00, 0x00, // colors used
                                               0x00, 0x00, 0x00, 0x00, // important colors
                                               // Color table (2 entries, BGRA): black, white
                                               0x00, 0x00, 0x00, 0x00, // black
                                               0xFF, 0xFF, 0xFF, 0x00, // white
                                               // Pixel data (bottom-up, 4-byte padded)
                                               0x00, 0x00, 0x00, 0x00};
        send_binary_response(tpcb, "image/bmp", bmp1x1, sizeof(bmp1x1));
        return;
    }

    int width = hdr->width;
    int height = hdr->height;
    const uint8_t *src = FLASH_PTR(LOGO_FLASH_OFFSET + sizeof(logo_header_t));

    // BMP row size in bytes (padded to 4-byte boundary)
    int row_bits = width;
    int row_bytes = (row_bits + 7) / 8;
    int row_padded = (row_bytes + 3) & ~3;
    int pixel_array_size = row_padded * height;
    int file_size = 14 + 40 + 8 + pixel_array_size;
    int offset_pixels = 14 + 40 + 8;

    uint8_t *bmp = (uint8_t *)malloc(file_size);
    if (!bmp) {
        send_response(tpcb, "<html><body><h3>OOM logo</h3></body></html>");
        return;
    }

    // BITMAPFILEHEADER
    uint8_t *d = bmp;
    *d++ = 'B';
    *d++ = 'M';
    d[0] = file_size & 0xFF;
    d[1] = (file_size >> 8) & 0xFF;
    d[2] = (file_size >> 16) & 0xFF;
    d[3] = (file_size >> 24) & 0xFF;
    d += 4;
    d[0] = d[1] = d[2] = d[3] = 0;
    d += 4; // reserved
    d[0] = offset_pixels & 0xFF;
    d[1] = (offset_pixels >> 8) & 0xFF;
    d[2] = (offset_pixels >> 16) & 0xFF;
    d[3] = (offset_pixels >> 24) & 0xFF;
    d += 4;
    // BITMAPINFOHEADER (40 bytes)
    d[0] = 40;
    d[1] = d[2] = d[3] = 0;
    d += 4; // header size
    d[0] = width & 0xFF;
    d[1] = (width >> 8) & 0xFF;
    d[2] = (width >> 16) & 0xFF;
    d[3] = (width >> 24) & 0xFF;
    d += 4;
    d[0] = height & 0xFF;
    d[1] = (height >> 8) & 0xFF;
    d[2] = (height >> 16) & 0xFF;
    d[3] = (height >> 24) & 0xFF;
    d += 4;
    d[0] = 1;
    d[1] = 0;
    d += 2; // planes
    d[0] = 1;
    d[1] = 0;
    d += 2; // bpp=1
    d[0] = d[1] = d[2] = d[3] = 0;
    d += 4; // compression BI_RGB
    d[0] = pixel_array_size & 0xFF;
    d[1] = (pixel_array_size >> 8) & 0xFF;
    d[2] = (pixel_array_size >> 16) & 0xFF;
    d[3] = (pixel_array_size >> 24) & 0xFF;
    d += 4;
    d[0] = 0x13;
    d[1] = 0x0B;
    d[2] = 0;
    d[3] = 0;
    d += 4; // ppm X
    d[0] = 0x13;
    d[1] = 0x0B;
    d[2] = 0;
    d[3] = 0;
    d += 4; // ppm Y
    d[0] = d[1] = d[2] = d[3] = 0;
    d += 4; // colors used
    d[0] = d[1] = d[2] = d[3] = 0;
    d += 4; // important colors
    // Color table (black, white) in BGRA
    *d++ = 0x00;
    *d++ = 0x00;
    *d++ = 0x00;
    *d++ = 0x00; // black
    *d++ = 0xFF;
    *d++ = 0xFF;
    *d++ = 0xFF;
    *d++ = 0x00; // white

    // Pixel data: bottom-up, left-to-right, MSB first in each byte, padded per row to 4 bytes
    uint8_t *pix = bmp + offset_pixels;
    for (int y = 0; y < height; y++) {
        int src_y = height - 1 - y; // bottom-up
        const uint8_t *src_row = src + (src_y * ((width + 7) / 8));
        int out_byte = 0;
        int bitpos = 7;
        for (int x = 0; x < width; x++) {
            int src_bit = 7 - (x % 8);
            int src_byte = x / 8;
            int bit = (src_row[src_byte] >> src_bit) & 1; // 1=black
            // In BMP 1bpp, bit=1 means white or black? Using palette order: index 0=black, 1=white,
            // so use inverse
            int idx = bit ? 0 : 1; // black→0, white→1
            out_byte |= (idx << bitpos);
            if (bitpos == 0) {
                *pix++ = (uint8_t)out_byte;
                out_byte = 0;
                bitpos = 7;
            } else {
                bitpos--;
            }
        }
        if (bitpos != 7) {
            *pix++ = (uint8_t)out_byte;
        }
        // pad row
        int written = (width + 7) / 8;
        while (written < row_padded) {
            *pix++ = 0x00;
            written++;
        }
    }

    // Send HTTP response (binary streaming)
    send_binary_response(tpcb, "image/bmp", bmp, file_size);
    free(bmp);
}

#ifdef USE_CASE_WEATHERMAP
static void handle_weathermap_png_route(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                                        int len) {
    // Serve the cached PNG staged in flash (slot1) if present
    uint32_t staged_bytes = 0;
    if (!get_weathermap_meta(&staged_bytes) || staged_bytes < 8) {
        send_response(tpcb, "<html><body><h3>No cached map</h3></body></html>");
        return;
    }
    const uint8_t *data = FLASH_PTR(FIRMWARE_SLOT1_FLASH_OFFSET);
    if (!(data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')) {
        send_response(tpcb, "<html><body><h3>Invalid PNG in cache</h3></body></html>");
        return;
    }
    send_binary_response(tpcb, "image/png", data, staged_bytes);
}
#endif

#ifdef USE_CASE_WEATHERMAP
static void handle_weathermap_clear_route(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                                          int len) {
    // Clear stored image and meta so a fresh fetch occurs next boot
    weathermap_flash_clear_image();
    clear_weathermap_meta();
    char page[512];
    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" "
             "content=\"width=device-width, initial-scale=1\">"
             "<title>Weathermap</title><style>body{font-family:sans-serif;margin:2em;}</style></"
             "head><body>"
             "<h3>Weathermap cleared</h3><p>The cached image and marker were erased. A fresh fetch "
             "will occur on next boot.</p>"
             "<p><a href=\"/\">Back</a></p>"
             "</body></html>");
    send_response(tpcb, page);
}
#endif

// =============================================================================
// CORE WEBSERVER FUNCTIONS
// =============================================================================

static absolute_time_t shutdown_time = {0};

// Shutdown callback
static int64_t shutdown_callback(alarm_id_t id, void *user_data) {
    extern ds3231_t ds3231;
    debug_log("Shutdown callback invoked\n");
    set_alarmclock_and_powerdown(&ds3231);
    // watchdog_reboot(0, 0, 0);
    return 0;
}

typedef struct {
    struct tcp_pcb *pcb;
    const char *ptr;
    size_t remaining;
    int chunk_index;
    size_t body_len;
    char *body_copy;
} response_state_t;

void webserver_set_shutdown_time(absolute_time_t t) { shutdown_time = t; }

void add_timeout_info(char *buf, size_t buf_size) {
    uint64_t now_us = to_us_since_boot(get_absolute_time());
    uint64_t target_us = to_us_since_boot(shutdown_time);

    if (target_us > now_us) {
        uint64_t diff_us = target_us - now_us;
        int seconds = (int)(diff_us / 1000000ULL);
        int minutes = seconds / 60;
        seconds %= 60;
        snprintf(buf, buf_size, "<small>Remaining time before shutdown: %d:%02d minutes</small>",
                 minutes, seconds);
    } else {
        snprintf(buf, buf_size, "<small>Setup period expired</small>");
    }
}

static err_t send_next_chunk(void *arg, struct tcp_pcb *tpcb, u16_t len);
static err_t send_next_chunk(void *arg, struct tcp_pcb *tpcb, u16_t len);
void send_response_with_content_type_and_disposition(struct tcp_pcb *tpcb, const char *body,
                                                     const char *content_type,
                                                     const char *content_disposition) {
    size_t body_len = strlen(body);
    // debug_log("send_response: body_len = %zu\n", body_len);

    // Copy HTML body to keep it valid
    char *body_copy = malloc(body_len + 1);
    if (!body_copy) {
        // debug_log("send_response: malloc failed for body\n");
        return;
    }
    memcpy(body_copy, body, body_len + 1);

    // HTTP-Header erzeugen
    char header[512];
    int header_len = 0;
    if (content_disposition && content_disposition[0] != '\0') {
        header_len = snprintf(
            header, sizeof(header),
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Disposition: %s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n",
            (content_type && content_type[0] != '\0') ? content_type : "text/html; charset=UTF-8",
            content_disposition, body_len);
    } else {
        header_len = snprintf(
            header, sizeof(header),
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n",
            (content_type && content_type[0] != '\0') ? content_type : "text/html; charset=UTF-8",
            body_len);
    }

    // debug_log("send_response: sending header (%d bytes)\n", header_len);
    err_t err = tcp_write(tpcb, header, header_len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        debug_log("send_response: tcp_write(header) failed with code %d\n", err);
        free(body_copy);
        return;
    }

    // Prepare state for sending
    response_state_t *state = malloc(sizeof(response_state_t));
    if (!state) {
        debug_log("send_response: malloc failed for state\n");
        free(body_copy);
        return;
    }

    state->pcb = tpcb;
    state->ptr = body_copy;
    state->remaining = body_len;
    state->chunk_index = 0;
    state->body_len = body_len;
    state->body_copy = body_copy;

    // Callback setzen und ersten Chunk senden
    tcp_arg(tpcb, state);
    tcp_sent(tpcb, send_next_chunk);
    send_next_chunk(state, tpcb, 0);
}

static void send_response_with_content_type(struct tcp_pcb *tpcb, const char *body,
                                            const char *content_type) {
    send_response_with_content_type_and_disposition(tpcb, body, content_type, NULL);
}

void send_response(struct tcp_pcb *tpcb, const char *body) {
    send_response_with_content_type(tpcb, body, "text/html; charset=UTF-8");
}

static err_t send_next_chunk(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    response_state_t *state = (response_state_t *)arg;

    if (state->remaining == 0) {
        debug_log("send_next_chunk: transfer completed.\n");
        tcp_output(tpcb);
        tcp_sent(tpcb, NULL);
        tcp_arg(tpcb, NULL);
        free(state->body_copy);
        free(state);
        tcp_close(tpcb);
        return ERR_OK;
    }

    u16_t chunk =
        (state->remaining > (size_t)TCP_CHUNK_SIZE) ? TCP_CHUNK_SIZE : (u16_t)state->remaining;
    err_t err = tcp_write(tpcb, state->ptr, chunk, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        debug_log("send_next_chunk: tcp_write chunk %d failed at %zu bytes remaining: err=%d\n",
                  state->chunk_index, state->remaining, err);
        free(state->body_copy);
        free(state);
        return err;
    }

    debug_log("send_next_chunk: chunk %d (%u bytes) written, %zu remaining\n", state->chunk_index,
              chunk, state->remaining - chunk);

    state->ptr += chunk;
    state->remaining -= chunk;
    state->chunk_index++;

    tcp_output(tpcb);
    return ERR_OK;
}

// Binary streaming (e.g., images) using lwIP tcp_sent backpressure
typedef struct {
    struct tcp_pcb *pcb;
    const uint8_t *ptr;
    size_t remaining;
} bin_response_state_t;

static err_t send_next_binary_chunk(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    bin_response_state_t *st = (bin_response_state_t *)arg;
    if (st->remaining == 0) {
        free(st);
        return ERR_OK;
    }
    u16_t chunk = st->remaining > TCP_CHUNK_SIZE ? TCP_CHUNK_SIZE : (u16_t)st->remaining;
    err_t err = tcp_write(tpcb, st->ptr, chunk, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        return err;
    }
    st->ptr += chunk;
    st->remaining -= chunk;
    tcp_output(tpcb);
    return ERR_OK;
}

static void send_binary_response(struct tcp_pcb *tpcb, const char *content_type,
                                 const uint8_t *data, size_t len) {
    char header[160];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.0 200 OK\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %u\r\n"
                              "Connection: close\r\n\r\n",
                              content_type, (unsigned)len);
    if (tcp_write(tpcb, header, header_len, TCP_WRITE_FLAG_COPY) != ERR_OK) {
        return;
    }
    bin_response_state_t *st = (bin_response_state_t *)malloc(sizeof(bin_response_state_t));
    if (!st)
        return;
    st->pcb = tpcb;
    st->ptr = data;
    st->remaining = len;
    tcp_arg(tpcb, st);
    tcp_sent(tpcb, send_next_binary_chunk);
    send_next_binary_chunk(st, tpcb, 0);
}

// =============================================================================
// POST ROUTE HANDLERS
// =============================================================================
//
// To add a new POST handler:
// 1. Add function declaration above in FORWARD DECLARATIONS section
// 2. Implement handler here following existing patterns
// 3. Add route entry in ROUTE TABLE section below
// 4. Form handlers: collect data, call handle_form_xxx() (create in webserver_pages.c)
// 5. Binary handlers: setup upload_session, let chunked logic handle it

#ifdef USE_CASE_SEATSURFING
static void handle_post_seatsurfing(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                                    int copied) {
    const char *cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD SEATSURFING CONFIG: Content-Length missing\n");
        send_seatsurfing_config_page(tpcb, "Missing Content-Length");
        tcp_close(tpcb);
        return;
    }

    upload_session.expected_length = atoi(cl + 15);
    if (upload_session.expected_length >= sizeof(upload_session.form_buffer)) {
        debug_log_with_color(COLOR_RED, "UPLOAD SEATSURFING CONFIG: form body too large\n");
        send_seatsurfing_config_page(tpcb, "Form data too large");
        tcp_close(tpcb);
        return;
    }

    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_FORM_SEATSURFING;

    const char *body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);
        memcpy(upload_session.form_buffer, body, body_len);
        upload_session.total_received = body_len;
        tcp_recved(tpcb, copied);

        debug_log("UPLOAD SEATSURFING CONFIG: First POST /seatsurfing body chunk (%d bytes)\n",
                  (int)body_len);

        if (upload_session.total_received >= upload_session.expected_length) {
            upload_session.form_buffer[upload_session.expected_length] = '\0';
            handle_form_seatsurfing(tpcb, upload_session.form_buffer,
                                    upload_session.expected_length);
            upload_session.active = false;
            upload_session.header_complete = false;
            upload_session.header_length = 0;
        }
    } else {
        debug_log_with_color(COLOR_RED, "UPLOAD SEATSURFING CONFIG: Header body split error\n");
        send_seatsurfing_config_page(tpcb, "Fehler beim Parsen des Formulars");
        tcp_close(tpcb);
    }
}
#elif defined(USE_CASE_HISTORIAN)
static void handle_post_historian(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                                  int copied) {
    const char *cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD HISTORIAN CONFIG: Content-Length missing\n");
        send_historian_config_page(tpcb, "Missing Content-Length");
        tcp_close(tpcb);
        return;
    }

    upload_session.expected_length = atoi(cl + 15);
    if (upload_session.expected_length >= sizeof(upload_session.form_buffer)) {
        debug_log_with_color(COLOR_RED, "UPLOAD HISTORIAN CONFIG: form body too large\n");
        send_historian_config_page(tpcb, "Form data too large");
        tcp_close(tpcb);
        return;
    }

    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_FORM_HISTORIAN;

    const char *body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);
        memcpy(upload_session.form_buffer, body, body_len);
        upload_session.total_received = body_len;
        tcp_recved(tpcb, copied);

        debug_log("UPLOAD HISTORIAN CONFIG: First POST /historian body chunk (%d bytes)\n",
                  (int)body_len);

        if (upload_session.total_received >= upload_session.expected_length) {
            upload_session.form_buffer[upload_session.expected_length] = '\0';
            handle_form_historian(tpcb, upload_session.form_buffer, upload_session.expected_length);
            upload_session.active = false;
            upload_session.header_complete = false;
            upload_session.header_length = 0;
        }
    } else {
        debug_log_with_color(COLOR_RED, "UPLOAD HISTORIAN CONFIG: Header body split error\n");
        send_historian_config_page(tpcb, "Fehler beim Parsen des Formulars");
        tcp_close(tpcb);
    }
}
#elif defined(USE_CASE_WEATHERMAP)
static void handle_post_weathermap(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                                   int copied) {
    const char *cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD WEATHERMAP CONFIG: Content-Length missing\n");
        send_weathermap_page(tpcb, "Missing Content-Length");
        tcp_close(tpcb);
        return;
    }

    upload_session.expected_length = atoi(cl + 15);
    if (upload_session.expected_length >= sizeof(upload_session.form_buffer)) {
        debug_log_with_color(COLOR_RED, "UPLOAD WEATHERMAP CONFIG: form body too large\n");
        send_weathermap_page(tpcb, "Form data too large");
        tcp_close(tpcb);
        return;
    }

    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_FORM_WEATHERMAP;

    const char *body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);
        memcpy(upload_session.form_buffer, body, body_len);
        upload_session.total_received = body_len;
        tcp_recved(tpcb, copied);

        debug_log("UPLOAD WEATHERMAP CONFIG: First POST /weathermap body chunk (%d bytes)\n",
                  (int)body_len);

        if (upload_session.total_received >= upload_session.expected_length) {
            upload_session.form_buffer[upload_session.expected_length] = '\0';
            handle_form_weathermap(tpcb, upload_session.form_buffer,
                                   upload_session.expected_length);
            upload_session.active = false;
            upload_session.header_complete = false;
            upload_session.header_length = 0;
        }
    } else {
        debug_log_with_color(COLOR_RED, "UPLOAD WEATHERMAP CONFIG: Header body split error\n");
        send_weathermap_page(tpcb, "Form parse error");
        tcp_close(tpcb);
    }
}
#endif

#ifdef USE_CASE_HOMEMATIC
static void handle_post_homematic(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                                  int copied) {
    const char *cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD HOMEMATIC CONFIG: Content-Length missing\n");
        send_homematic_config_page(tpcb, "Missing Content-Length");
        tcp_close(tpcb);
        return;
    }

    upload_session.expected_length = atoi(cl + 15);
    if (upload_session.expected_length >= sizeof(upload_session.form_buffer)) {
        debug_log_with_color(COLOR_RED, "UPLOAD HOMEMATIC CONFIG: form body too large\n");
        send_homematic_config_page(tpcb, "Form data too large");
        tcp_close(tpcb);
        return;
    }

    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_FORM_HOMEMATIC;

    const char *body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);
        memcpy(upload_session.form_buffer, body, body_len);
        upload_session.total_received = body_len;
        tcp_recved(tpcb, copied);

        debug_log("UPLOAD HOMEMATIC CONFIG: First POST /homematic body chunk (%d bytes)\n",
                  (int)body_len);

        if (upload_session.total_received >= upload_session.expected_length) {
            upload_session.form_buffer[upload_session.expected_length] = '\0';
            handle_form_homematic(tpcb, upload_session.form_buffer, upload_session.expected_length);
            upload_session.active = false;
            upload_session.header_complete = false;
            upload_session.header_length = 0;
        }
    } else {
        debug_log_with_color(COLOR_RED, "UPLOAD HOMEMATIC CONFIG: Header body split error\n");
        send_homematic_config_page(tpcb, "Fehler beim Parsen des Formulars");
        tcp_close(tpcb);
    }
}
#endif

static void handle_post_message(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                                int copied) {
    const char *cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD MESSAGE: Content-Length missing\n");
        send_message_page(tpcb, "Missing Content-Length");
        tcp_close(tpcb);
        return;
    }

    upload_session.expected_length = atoi(cl + 15);
    if (upload_session.expected_length >= sizeof(upload_session.form_buffer)) {
        debug_log_with_color(COLOR_RED, "UPLOAD MESSAGE: form body too large\n");
        send_message_page(tpcb, "Form data too large");
        tcp_close(tpcb);
        return;
    }

    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_FORM_MESSAGE;

    const char *body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);
        memcpy(upload_session.form_buffer, body, body_len);
        upload_session.total_received = body_len;
        tcp_recved(tpcb, copied);

        debug_log("UPLOAD MESSAGE: First POST /message body chunk (%d bytes)\n", (int)body_len);

        if (upload_session.total_received >= upload_session.expected_length) {
            upload_session.form_buffer[upload_session.expected_length] = '\0';
            handle_form_message(tpcb, upload_session.form_buffer, upload_session.expected_length);
            upload_session.active = false;
            upload_session.header_complete = false;
            upload_session.header_length = 0;
        }
    } else {
        debug_log_with_color(COLOR_RED, "UPLOAD MESSAGE: Header body split error\n");
        send_message_page(tpcb, "Fehler beim Parsen des Formulars");
        tcp_close(tpcb);
    }
}

static void handle_post_settings_import(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                                        int copied) {
    const char *cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD SETTINGS IMPORT: Content-Length missing\n");
        send_settings_transfer_page(tpcb, "Missing Content-Length");
        tcp_close(tpcb);
        return;
    }

    upload_session.expected_length = atoi(cl + 15);
    if (upload_session.expected_length >= sizeof(upload_session.form_buffer)) {
        debug_log_with_color(COLOR_RED, "UPLOAD SETTINGS IMPORT: form body too large\n");
        send_settings_transfer_page(tpcb, "Form data too large");
        tcp_close(tpcb);
        return;
    }

    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_FORM_SETTINGS_IMPORT;

    const char *body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);
        memcpy(upload_session.form_buffer, body, body_len);
        upload_session.total_received = body_len;
        tcp_recved(tpcb, copied);

        debug_log("UPLOAD SETTINGS IMPORT: First POST /settings_import body chunk (%d bytes)\n",
                  (int)body_len);

        if (upload_session.total_received >= upload_session.expected_length) {
            upload_session.form_buffer[upload_session.expected_length] = '\0';
            handle_form_settings_import(tpcb, upload_session.form_buffer,
                                        upload_session.expected_length);
            upload_session.active = false;
            upload_session.header_complete = false;
            upload_session.header_length = 0;
        }
    } else {
        debug_log_with_color(COLOR_RED, "UPLOAD SETTINGS IMPORT: Header body split error\n");
        send_settings_transfer_page(tpcb, "Form parse error");
        tcp_close(tpcb);
    }
}

static void handle_post_device_config(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                                      int copied) {
    const char *cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD DEVICE CONFIG: Content-Length missing\n");
        send_device_config_page(tpcb, "Fehlender Content-Length");
        tcp_close(tpcb);
        return;
    }

    upload_session.expected_length = atoi(cl + 15);
    if (upload_session.expected_length >= sizeof(upload_session.form_buffer)) {
        debug_log_with_color(COLOR_RED, "UPLOAD DEVICE CONFIG: form body too large\n");
        send_device_config_page(tpcb, "Form data too large");
        tcp_close(tpcb);
        return;
    }

    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_FORM_DEVICE;

    const char *body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);
        memcpy(upload_session.form_buffer, body, body_len);
        upload_session.total_received = body_len;
        tcp_recved(tpcb, copied);

        debug_log("UPLOAD DEVICE CONFIG: First POST /device_config body chunk (%d bytes)\n",
                  (int)body_len);

        if (upload_session.total_received >= upload_session.expected_length) {
            upload_session.form_buffer[upload_session.expected_length] = '\0';
            handle_form_device_config(tpcb, upload_session.form_buffer,
                                      upload_session.expected_length);
            upload_session.active = false;
            upload_session.header_complete = false;
            upload_session.header_length = 0;
        }
    } else {
        debug_log_with_color(COLOR_RED, "UPLOAD DEVICE CONFIG: Header body split error\n");
        send_device_config_page(tpcb, "Fehler beim Parsen des Formulars");
        tcp_close(tpcb);
    }
}

static void handle_post_upload_logo(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                                    int copied) {
    const char *cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD LOGO: Content-Length missing\n");
        send_upload_logo_page(tpcb, "missing_content_length");
        reset_upload_session();
        tcp_close(tpcb);
        return;
    }

    upload_session.expected_length = atoi(cl + 15);
    debug_log("UPLOAD LOGO: Expected length: %d\n", (int)upload_session.expected_length);

    if (upload_session.expected_length > LOGO_FLASH_SIZE) {
        debug_log_with_color(COLOR_RED, "UPLOAD LOGO: File too large (%d > %d bytes)\n",
                             upload_session.expected_length, LOGO_FLASH_SIZE);
        send_upload_logo_page(tpcb, "too_large");
        reset_upload_session();
        tcp_arg(tpcb, NULL);
        tcp_recv(tpcb, NULL);
        tcp_close(tpcb);
        return;
    }

    flash_writer.buffer_filled = 0;
    flash_writer.flash_offset = LOGO_FLASH_OFFSET;

    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_LOGO;
    upload_session.flash_offset = LOGO_FLASH_OFFSET;

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(upload_session.flash_offset, LOGO_FLASH_SIZE);
    restore_interrupts(ints);

    debug_log("UPLOAD LOGO: flash erased: %d\n", (int)upload_session.expected_length);

    const char *body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (!body) {
        debug_log_with_color(COLOR_RED, "UPLOAD LOGO: Body not found despite complete header?\n");
        send_upload_logo_page(tpcb, "invalid_request");
        reset_upload_session();
        tcp_close(tpcb);
        return;
    }

    body += 4;
    size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);

    const uint8_t *ptr = (const uint8_t *)body;
    size_t to_copy = body_len;

    while (to_copy > 0) {
        size_t space = FLASH_PAGE_SIZE - flash_writer.buffer_filled;
        size_t chunk = (to_copy < space) ? to_copy : space;

        memcpy(flash_writer.buffer + flash_writer.buffer_filled, ptr, chunk);
        flash_writer.buffer_filled += chunk;
        ptr += chunk;
        to_copy -= chunk;

        if (flash_writer.buffer_filled == FLASH_PAGE_SIZE) {
            flush_page_to_flash();
        }
    }
    upload_session.total_received += body_len;

    debug_log("UPLOAD LOGO: First chunk written (%d bytes)\n", (int)body_len);
    tcp_recved(tpcb, copied);
}

static void handle_post_firmware_update(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                                        int copied) {
    const char *cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD FIRMWARE: Content-Length missing\n");
        send_firmware_update_page(tpcb, "<h2 style='color:red'>❌ Missing Content-Length</h2>");
        reset_upload_session();
        tcp_recved(tpcb, copied);
        return;
    }

    char *endptr = NULL;
    unsigned long expected = strtoul(cl + 15, &endptr, 10);
    if (endptr == cl + 15 || expected == 0 || expected > FIRMWARE_FLASH_SIZE) {
        debug_log_with_color(COLOR_RED, "UPLOAD FIRMWARE: Invalid Content-Length (%s)\n", cl + 15);
        send_firmware_update_page(tpcb, "too_large");
        reset_upload_session();
        tcp_recved(tpcb, copied);
        return;
    }

    upload_session.expected_length = (size_t)expected;
    debug_log("UPLOAD FIRMWARE: Expected length: %d\n", (int)upload_session.expected_length);
    // Determine target slot based on get_active_firmware_slot_info()
    const char *slot_info = get_active_firmware_slot_info();
    uint32_t target_offset;

    if (strncmp(slot_info, "SLOT_0", 6) == 0) {
        target_offset = FIRMWARE_SLOT1_FLASH_OFFSET;
    } else {
        // SLOT_1 or unknown/direct: write to SLOT_0
        target_offset = FIRMWARE_SLOT0_FLASH_OFFSET;
    }

    // Calculate flash erase length (aligned to sector size)
    // size_t erase_length = (upload_session.expected_length + FLASH_SECTOR_SIZE - 1) &
    // ~(FLASH_SECTOR_SIZE - 1); // this is not enough, because of padding to fill the page!

    size_t erase_length =
        ((upload_session.expected_length + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1)) +
        FLASH_SECTOR_SIZE;

    // Prepare flash writer
    flash_writer.buffer_filled = 0;
    flash_writer.flash_offset = target_offset;

    // Initialize upload session
    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_FIRMWARE;
    upload_session.flash_offset = target_offset;

    debug_log("UPLOAD FIRMWARE: Writing to offset 0x%08X (active = %.*s)\n", target_offset, 6,
              slot_info);

    size_t num_sectors = (erase_length + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    size_t estimated_erase_time_ms = num_sectors * 38U;

    size_t write_blocks = (upload_session.expected_length + 255U) / 256U;
    size_t estimated_write_time_ms = write_blocks;

    size_t estimated_total_ms = estimated_erase_time_ms + estimated_write_time_ms;
    if (estimated_total_ms > (size_t)INT_MAX) {
        estimated_total_ms = (size_t)INT_MAX;
    }
    upload_session.flash_estimated_duration = (int)estimated_total_ms;
    g_firmware_progress_logged = -10;

    debug_log("UPLOAD FIRMWARE: estimate erase=%zu ms, write=%zu ms → total %d ms\n",
              estimated_erase_time_ms, estimated_write_time_ms,
              upload_session.flash_estimated_duration);

    if (erase_length > FIRMWARE_FLASH_SIZE) {
        debug_log("ERROR: erase_length (%u) exceeds FIRMWARE_FLASH_SIZE (%u), aborting erase!\n",
                  erase_length, FIRMWARE_FLASH_SIZE);
        send_firmware_update_page(
            tpcb, "<h2 style='color:red'>❌ Firmware upload aborted (erase range invalid)</h2>");
        reset_upload_session();
        tcp_recved(tpcb, copied);
        return;
    }

    size_t erased = 0;
    while (erased < erase_length) {
        watchdog_update();
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(upload_session.flash_offset + erased, FLASH_SECTOR_SIZE);
        restore_interrupts(ints);
        erased += FLASH_SECTOR_SIZE;
    }
    watchdog_update();

    debug_log("UPLOAD FIRMWARE: flash erased: %d\n", (int)upload_session.expected_length);

    const char *body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (!body) {
        debug_log_with_color(COLOR_RED,
                             "UPLOAD FIRMWARE: Body not found despite complete header\n");
        send_firmware_update_page(tpcb,
                                  "<h2 style='color:red'>❌ Upload parse error (no body)</h2>");
        reset_upload_session();
        tcp_recved(tpcb, copied);
        return;
    }

    body += 4;
    size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);
    if (body_len > upload_session.expected_length) {
        body_len = upload_session.expected_length;
    }

    const uint8_t *ptr = (const uint8_t *)body;
    size_t to_copy = body_len;

    while (to_copy > 0) {
        size_t space = FLASH_PAGE_SIZE - flash_writer.buffer_filled;
        size_t chunk = (to_copy < space) ? to_copy : space;

        memcpy(flash_writer.buffer + flash_writer.buffer_filled, ptr, chunk);
        flash_writer.buffer_filled += chunk;
        ptr += chunk;
        to_copy -= chunk;

        if (flash_writer.buffer_filled == FLASH_PAGE_SIZE) {
            flush_page_to_flash();
        }
    }
    upload_session.total_received += body_len;

    debug_log("UPLOAD FIRMWARE: First chunk written (%d bytes)\n", (int)body_len);
    tcp_recved(tpcb, copied);
}

static void handle_post_wifi(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer, int copied) {
    const char *cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD WIFI CONFIG: Content-Length missing\n");
        send_wifi_config_page(tpcb, "");
        tcp_close(tpcb);
        return;
    }

    upload_session.expected_length = atoi(cl + 15);

    if (upload_session.expected_length >= sizeof(upload_session.form_buffer)) {
        debug_log_with_color(COLOR_RED, "UPLOAD WIFI CONFIG: form body too large\n");
        send_wifi_config_page(tpcb, ""); // ggf. mit Fehlerhinweis
        tcp_close(tpcb);
        return;
    }

    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_FORM_WIFI;

    const char *body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);
        memcpy(upload_session.form_buffer, body, body_len);
        upload_session.total_received = body_len;
        tcp_recved(tpcb, copied);
        debug_log("UPLOAD WIFI CONFIG: First POST /wifi body chunk (%d bytes)\n", (int)body_len);

        if (upload_session.total_received >= upload_session.expected_length) {
            upload_session.form_buffer[upload_session.expected_length] = '\0';
            handle_form_wifi(tpcb, upload_session.form_buffer, upload_session.expected_length);
            upload_session.active = false;
            upload_session.header_complete = false;
            upload_session.header_length = 0;
        }
    }
}

static void handle_post_clock(struct tcp_pcb *tpcb, struct pbuf *p, const char *buffer,
                              int copied) {
    const char *cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD CLOCK: Content-Length fehlt\n");
        send_clock_page(tpcb, "❌ Content-Length fehlt.");
        tcp_close(tpcb);
        return;
    }

    upload_session.expected_length = atoi(cl + 15);
    if (upload_session.expected_length >= sizeof(upload_session.form_buffer)) {
        debug_log_with_color(COLOR_RED, "UPLOAD CLOCK: body too large\n");
        send_clock_page(tpcb, "❌ Form data too large.");
        tcp_close(tpcb);
        return;
    }

    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_FORM_CLOCK;

    const char *body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);
        memcpy(upload_session.form_buffer, body, body_len);
        upload_session.total_received = body_len;
        tcp_recved(tpcb, copied);
        debug_log("UPLOAD CLOCK: First body chunk (%d bytes)\n", (int)body_len);

        if (upload_session.total_received >= upload_session.expected_length) {
            upload_session.form_buffer[upload_session.expected_length] = '\0';
            handle_form_clock(tpcb, upload_session.form_buffer, upload_session.expected_length);
            upload_session.active = false;
            upload_session.header_complete = false;
            upload_session.header_length = 0;
        }
    }
}

// =============================================================================
// ROUTE TABLE & DISPATCH
// =============================================================================

// Route table implementation
static const route_t routes[] = {
    // GET routes
    {"/", HTTP_GET, ROUTE_SIMPLE, {.simple_handler = send_landing_page}},
    {"/wifi", HTTP_GET, ROUTE_SIMPLE, {.simple_handler = send_wifi_config_page_wrapper}},
#ifdef USE_CASE_SEATSURFING
    {"/seatsurfing",
     HTTP_GET,
     ROUTE_SIMPLE,
     {.simple_handler = send_seatsurfing_config_page_wrapper}},
#elif defined(USE_CASE_HISTORIAN)
    {"/historian", HTTP_GET, ROUTE_SIMPLE, {.simple_handler = send_historian_config_page_wrapper}},
#elif defined(USE_CASE_HOMEMATIC)
    {"/homematic", HTTP_GET, ROUTE_SIMPLE, {.simple_handler = send_homematic_config_page_wrapper}},
#endif
    {"/device_settings",
     HTTP_GET,
     ROUTE_SIMPLE,
     {.simple_handler = send_device_config_page_wrapper}},
    {"/clock", HTTP_GET, ROUTE_SIMPLE, {.simple_handler = send_clock_page_wrapper}},
    {"/device_status", HTTP_GET, ROUTE_SIMPLE, {.simple_handler = send_device_status_page}},
    {"/upload_logo", HTTP_GET, ROUTE_SIMPLE, {.simple_handler = send_upload_logo_page_wrapper}},
    {"/firmware_update",
     HTTP_GET,
     ROUTE_SIMPLE,
     {.simple_handler = send_firmware_update_page_wrapper}},
    {"/settings_transfer",
     HTTP_GET,
     ROUTE_SIMPLE,
     {.simple_handler = send_settings_transfer_page_wrapper}},
    {"/settings_export.txt",
     HTTP_GET,
     ROUTE_SIMPLE,
     {.simple_handler = send_settings_export_txt_wrapper}},
    {"/logo", HTTP_GET, ROUTE_INLINE, {.inline_handler = handle_logo_route}},
    {"/shutdown", HTTP_GET, ROUTE_INLINE, {.inline_handler = handle_shutdown_route}},

#ifdef USE_CASE_WEATHERMAP
    {"/weathermap", HTTP_GET, ROUTE_SIMPLE, {.simple_handler = send_weathermap_page_wrapper}},
    {"/weathermap.png", HTTP_GET, ROUTE_INLINE, {.inline_handler = handle_weathermap_png_route}},
    {"/weathermap_clear",
     HTTP_GET,
     ROUTE_INLINE,
     {.inline_handler = handle_weathermap_clear_route}},
#endif

    // ADD NEW GET ROUTES HERE:
    // 1. Create send_new_page() function in webserver_pages.c
    // 2. Add declaration to webserver_pages.h
    // 3. Add route: {"/new_page", HTTP_GET, ROUTE_SIMPLE, {.simple_handler = send_new_page}},
    {"/message", HTTP_GET, ROUTE_SIMPLE, {.simple_handler = send_message_page_wrapper}},

    // POST routes
    {"/delete_logo", HTTP_POST, ROUTE_INLINE, {.inline_handler = handle_delete_logo_route}},
    {"/wifi", HTTP_POST, ROUTE_FORM, {.binary_handler = handle_post_wifi}},
#ifdef USE_CASE_SEATSURFING
    {"/seatsurfing", HTTP_POST, ROUTE_FORM, {.binary_handler = handle_post_seatsurfing}},
#elif defined(USE_CASE_HISTORIAN)
    {"/historian", HTTP_POST, ROUTE_FORM, {.binary_handler = handle_post_historian}},
#elif defined(USE_CASE_WEATHERMAP)
    {"/weathermap", HTTP_POST, ROUTE_FORM, {.binary_handler = handle_post_weathermap}},
#elif defined(USE_CASE_HOMEMATIC)
    {"/homematic", HTTP_POST, ROUTE_FORM, {.binary_handler = handle_post_homematic}},
#endif
    {"/device_config", HTTP_POST, ROUTE_FORM, {.binary_handler = handle_post_device_config}},
    {"/clock", HTTP_POST, ROUTE_FORM, {.binary_handler = handle_post_clock}},
    {"/upload_logo", HTTP_POST, ROUTE_BINARY, {.binary_handler = handle_post_upload_logo}},
    {"/firmware_update", HTTP_POST, ROUTE_BINARY, {.binary_handler = handle_post_firmware_update}},
    {"/firmware_demote_active",
     HTTP_POST,
     ROUTE_INLINE,
     {.inline_handler = handle_firmware_demote_active_route}},
    {"/settings_import", HTTP_POST, ROUTE_FORM, {.binary_handler = handle_post_settings_import}},

    // ADD NEW POST ROUTES HERE:
    // Form handlers: 1. Create handle_form_xxx() in webserver_pages.c, 2. Add handle_post_xxx()
    // here, 3. Add route
    // Binary uploads: 1. Add handle_post_xxx() here (setup upload_session), 2. Add route
    // Special actions: 1. Add handle_xxx_route() above, 2. Add route
    // Examples:
    // {"/new_form", HTTP_POST, ROUTE_FORM, {.binary_handler = handle_post_new_form}},
    // {"/new_upload", HTTP_POST, ROUTE_BINARY, {.binary_handler = handle_post_new_upload}},
    // {"/new_action", HTTP_POST, ROUTE_INLINE, {.inline_handler = handle_new_action_route}},
    {"/message", HTTP_POST, ROUTE_FORM, {.binary_handler = handle_post_message}},
};

static const size_t num_routes = sizeof(routes) / sizeof(routes[0]);

// Route matching function
static const route_t *find_route(const char *path, http_method_t method) {
    for (size_t i = 0; i < num_routes; i++) {
        if (routes[i].method == method && strcmp(routes[i].path, path) == 0) {
            return &routes[i];
        }
    }
    return NULL;
}

// Route dispatch function
static bool dispatch_route(const route_t *route, struct tcp_pcb *tpcb, struct pbuf *p,
                           const char *buffer, int len) {
    if (!route)
        return false;

    switch (route->type) {
    case ROUTE_SIMPLE:
        route->handler.simple_handler(tpcb);
        break;
    case ROUTE_FORM:
        // Extract body for form handlers - this would need proper implementation
        route->handler.form_handler(tpcb, buffer, len);
        break;
    case ROUTE_BINARY:
        route->handler.binary_handler(tpcb, p, buffer, len);
        break;
    case ROUTE_INLINE:
        route->handler.inline_handler(tpcb, p, buffer, len);
        break;
    }
    return true;
}

// =============================================================================
// MAIN HTTP REQUEST HANDLER
// =============================================================================

/*
 * recv_cb():
 * ├── Connection check
 * ├── Header collection phase
 * │   ├── Buffer header data
 * │   ├── Parse HTTP request line
 * │   ├── Route table dispatch
 * │   └── Error handling
 * ├── Binary upload chunked processing
 * │   ├── UPLOAD_LOGO (with helper)
 * │   └── UPLOAD_FIRMWARE (with helper + progress)
 * ├── Form upload chunked processing
 * │   └── All form types (with unified helper)
 * └── Upload completion handling
 */
static err_t recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char buffer[1501];
    int copied = pbuf_copy_partial(p, buffer, sizeof(buffer) - 1, 0);
    if (copied < 0)
        copied = 0;
    buffer[copied] = '\0';

    // collect header
    if (!upload_session.header_complete) {
        if (upload_session.header_length + copied < sizeof(upload_session.header_buffer)) {
            memcpy(upload_session.header_buffer + upload_session.header_length, buffer, copied);
            upload_session.header_length += copied;
            upload_session.header_buffer[upload_session.header_length] = '\0';

            debug_log("HEADER: collected %d bytes, total %d\n", copied,
                      (int)upload_session.header_length);

            const char *end = strstr(upload_session.header_buffer, "\r\n\r\n");
            if (!end) {
                // Header not yet complete
                tcp_recved(tpcb, copied);
                pbuf_free(p);
                return ERR_OK;
            }

            upload_session.header_complete = true;
            const char *route_line = upload_session.header_buffer;
            const char *eol = strstr(route_line, "\r\n"); // Extract only the first header line
            if (!eol) {
                debug_log_with_color(COLOR_RED, "HEADER: malformed - no CRLF\n");
                tcp_recved(tpcb, copied);
                pbuf_free(p);
                return ERR_OK;
            }

            size_t line_len = eol - route_line;
            if (line_len >= 128)
                line_len = 127; // Safety limit

            char first_line[128];
            memcpy(first_line, route_line, line_len);
            first_line[line_len] = '\0';

            debug_log("HEADER LINE: %s\n", first_line);

            // Try route table dispatch
            char method_str[8], path_str[64];
            if (sscanf(first_line, "%7s %63s", method_str, path_str) == 2) {
                http_method_t method = (strcmp(method_str, "GET") == 0)    ? HTTP_GET
                                       : (strcmp(method_str, "POST") == 0) ? HTTP_POST
                                                                           : -1;

                if (method != -1) {
                    const route_t *route = find_route(path_str, method);
                    if (route) {
                        dispatch_route(route, tpcb, p, buffer, copied);
                        debug_log("ROUTE TABLE: Handled %s %s\n", method_str, path_str);

                        // Only GET routes and special POST routes need immediate cleanup
                        if (route->type == ROUTE_SIMPLE || route->type == ROUTE_INLINE) {
                            cleanup_and_return(tpcb, p, copied);
                            return ERR_OK;
                        }
                        // Form/binary handlers set upload_session.active - let chunked logic handle
                        // them
                    } else {
                        // Route not found
                        debug_log_with_color(COLOR_RED, "Route not implemented: %s %s\n",
                                             method_str, path_str);
                        cleanup_and_return(tpcb, p, copied);
                        return ERR_OK;
                    }
                }
            } else {
                // Parse error - malformed request
                debug_log_with_color(COLOR_RED, "Malformed request: %s\n", first_line);
                cleanup_and_return(tpcb, p, copied);
                return ERR_OK;
            }
        } else {
            debug_log_with_color(COLOR_RED, "HEADER: buffer overflow\n");
            tcp_recved(tpcb, copied);
            pbuf_free(p);
            return ERR_OK;
        }
    } else if (upload_session.active && upload_session.type == UPLOAD_LOGO) {
        process_binary_upload_chunk(buffer, copied, "LOGO");
        tcp_recved(tpcb, copied);
        debug_log("UPLOAD LOGO: Additional chunk (%d bytes, total %d)\n", copied,
                  (int)upload_session.total_received);
    } else if (upload_session.active && upload_session.type == UPLOAD_FIRMWARE) {
        process_binary_upload_chunk(buffer, copied, "FIRMWARE");
        tcp_recved(tpcb, copied);

        // Progress logging for firmware updates
        if (upload_session.expected_length > 0) {
            int percent =
                (int)((100ULL * upload_session.total_received) / upload_session.expected_length);
            if (percent >= g_firmware_progress_logged + 10) {
                g_firmware_progress_logged = percent;
                debug_log("UPLOAD FIRMWARE: Progress = %d%%\n", percent);
            }
        }
    } else if (upload_session.active && (upload_session.type == UPLOAD_FORM_WIFI ||
#ifdef USE_CASE_SEATSURFING
                                         upload_session.type == UPLOAD_FORM_SEATSURFING ||
#endif
#ifdef USE_CASE_HISTORIAN
                                         upload_session.type == UPLOAD_FORM_HISTORIAN ||
#endif
#ifdef USE_CASE_HOMEMATIC
                                         upload_session.type == UPLOAD_FORM_HOMEMATIC ||
#endif
#ifdef USE_CASE_WEATHERMAP
                                         upload_session.type == UPLOAD_FORM_WEATHERMAP ||
#endif
                                         upload_session.type == UPLOAD_FORM_DEVICE ||
                                         upload_session.type == UPLOAD_FORM_CLOCK ||
                                         upload_session.type == UPLOAD_FORM_SETTINGS_IMPORT ||
                                         upload_session.type == UPLOAD_FORM_MESSAGE)) {
        process_form_upload_chunk(buffer, copied, tpcb);
    }
    if (upload_session.active && upload_session.total_received >= upload_session.expected_length) {
        debug_log_with_color(COLOR_GREEN, "UPLOAD: Complete (%d bytes)\n",
                             (int)upload_session.total_received);

        flush_page_to_flash(); // Write last incomplete page
        __dsb();
        __isb();
        // sleep_ms(50);  // Time for Cache/Flash sync

        // Debug logging
        debug_log("FLASH end offset: 0x%X\n", flash_writer.flash_offset);

        if (upload_session.type == UPLOAD_FIRMWARE) {
            char msg[1024];
            firmware_header_t header;
            memcpy(&header, FLASH_PTR(upload_session.flash_offset), sizeof(header));

            // Check 1: Magic word
            bool valid = memcmp(header.magic, FIRMWARE_MAGIC, FIRMWARE_MAGIC_LEN) == 0;

            if (!valid) {
                debug_log_with_color(
                    COLOR_RED, "FIRMWARE: Invalid header detected after upload – disabling slot\n");
                send_firmware_update_page(
                    tpcb, "<h2 style='color:red'>❌ FIRMWARE: Invalid header (magic) </h2>");
            }

            // Check that the firmware header claims the correct slot
            uint8_t expected_slot = (upload_session.flash_offset == FIRMWARE_SLOT0_FLASH_OFFSET) ? 0
                                    : (upload_session.flash_offset == FIRMWARE_SLOT1_FLASH_OFFSET)
                                        ? 1
                                        : 255;

            if (valid) { // only test slot for actual firmware (magic word) files
                if (header.slot != expected_slot) {
                    debug_log_with_color(COLOR_RED,
                                         "FIRMWARE: Slot mismatch – header says slot %u, expected "
                                         "slot %u based on upload target 0x%X\n",
                                         header.slot, expected_slot, upload_session.flash_offset);
                    send_firmware_update_page(
                        tpcb, "<h2 style='color:red'>❌ Slot mismatch - invalid firmware!</h2>");
                }
            }

            bool crc_ok = false;
            if (valid && header.slot == expected_slot) { // only test crc32 for actual firmware
                                                         // (magic word) files and correct slot
                const uint8_t *firmware_data =
                    (const uint8_t *)(XIP_BASE + upload_session.flash_offset);
                const uint32_t actual_crc = crc32_calculate(firmware_data, header.firmware_size);

                debug_log("CRC calc: addr = 0x%08X, header.firmware_size = %u\n",
                          (unsigned)(uintptr_t)firmware_data, (unsigned)header.firmware_size);

                crc_ok = (actual_crc == header.crc32);
                if (crc_ok) {
                    debug_log("CRC check OK: 0x%08X\n", actual_crc);
                } else {
                    debug_log("CRC MISMATCH: expected 0x%08X, got 0x%08X\n", header.crc32,
                              actual_crc);
                    snprintf(msg, sizeof(msg),
                             "<h2 style='color:red'>CRC MISMATCH: expected 0x%08X, got 0x%08X</h2>",
                             header.crc32, actual_crc);
                    send_firmware_update_page(tpcb, msg);
                }
            }

            bool use_case_meta_valid = firmware_header_use_case_meta_valid(&header);
            bool use_case_mismatch = false;
            if (use_case_meta_valid) {
                use_case_mismatch = (header.use_case_id != USE_CASE_ID);
                if (use_case_mismatch) {
                    debug_log_with_color(
                        COLOR_YELLOW,
                        "FIRMWARE: Use-case mismatch (uploaded: %s/%u, running: %s/%u)\n",
                        header.use_case_name, (unsigned)header.use_case_id, USE_CASE_NAME,
                        (unsigned)USE_CASE_ID);
                }
            } else if (header.meta_version != 0 || header.use_case_id != 0 ||
                       header.use_case_name[0] != '\0') {
                debug_log_with_color(COLOR_YELLOW,
                                     "FIRMWARE: Ignoring invalid/unknown use-case metadata "
                                     "(version=%u, id=%u, name='%.*s')\n",
                                     (unsigned)header.meta_version, (unsigned)header.use_case_id,
                                     (int)sizeof(header.use_case_name), header.use_case_name);
            }

            if (valid && crc_ok && header.slot == expected_slot) {
                debug_log("Valid Firmware - you may now reboot from the new version!\n");
                mark_firmware_valid(upload_session.flash_offset);
                bool settings_zeroed = false;
                if (use_case_mismatch) {
                    settings_zeroed = zero_active_use_case_settings_sector();
                }

                char use_case_html[160];
                if (use_case_meta_valid) {
                    snprintf(use_case_html, sizeof(use_case_html),
                             "Use Case: <code>%s</code> (id %u)<br>", header.use_case_name,
                             (unsigned)header.use_case_id);
                } else {
                    snprintf(use_case_html, sizeof(use_case_html),
                             "Use Case: <code>legacy/unknown metadata</code><br>");
                }

                char warning_html[448] = "";
                if (use_case_mismatch) {
                    snprintf(warning_html, sizeof(warning_html),
                             "<p style='color:orange'><b>Warning:</b> Uploaded use case "
                             "(<code>%s</code>, id %u) differs from running use case "
                             "(<code>%s</code>, id %u). %s</p>",
                             header.use_case_name, (unsigned)header.use_case_id, USE_CASE_NAME,
                             (unsigned)USE_CASE_ID,
                             settings_zeroed
                                 ? "Use-case settings were reset to 0 and must be reconfigured."
                                 : "Some settings will be undefined!");
                }

                snprintf(msg, sizeof(msg),
                         "<h2 style='color:green'>Valid Firmware – you may now reboot from the new "
                         "version!</h2>"
                         "<p>"
                         "Version: <code>%s</code><br>"
                         "Build Date: <code>%s</code><br>"
                         "Size: <code>%u bytes</code><br>"
                         "CRC32: <code>0x%08X</code><br>"
                         "Slot: <code>%u</code><br>"
                         "%s"
                         "</p>"
                         "%s",
                         header.git_version, header.build_date, header.firmware_size, header.crc32,
                         header.slot, use_case_html, warning_html);

                send_firmware_update_page(tpcb, msg);
            }
        } else {
            const char *ok_msg =
                "<html><body><h2>✅ Upload OK</h2><a href='/'>Back</a></body></html>";
            send_response(tpcb, ok_msg);
        }

        reset_upload_session();
        upload_session.active = false;
        upload_session.header_complete = false;
        upload_session.header_length = 0;

        tcp_recved(tpcb, copied);
    }
    pbuf_free(p);
    return ERR_OK;
}

static err_t accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, recv_cb);
    return ERR_OK;
}

// =============================================================================
// WEBSERVER SETUP & INITIALIZATION
// =============================================================================

void start_setup_webserver() {
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb)
        return;

    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) {
        tcp_close(pcb);
        return;
    }

    pcb = tcp_listen(pcb);
    tcp_accept(pcb, accept_cb);
}

// =============================================================================
// FLASH OPERATIONS
// =============================================================================

static void mark_firmware_valid(uint32_t flash_offset) {
    // Read the 4 KB flash sector that contains the firmware header
    uint8_t sector_buffer[FLASH_SECTOR_SIZE];
    memcpy(sector_buffer, FLASH_PTR(flash_offset), FLASH_SECTOR_SIZE);

    // Patch the valid_flag inside the firmware_header_t
    firmware_header_t *header = (firmware_header_t *)sector_buffer;

    debug_log("Firmware header before setting valid_flag:\n");
    debug_log("  magic         : '%.*s'\n", (int)sizeof(header->magic), header->magic);
    debug_log("  valid_flag    : %u\n", header->valid_flag);
    debug_log("  build_date    : '%.*s'\n", (int)sizeof(header->build_date), header->build_date);
    debug_log("  git_version   : '%.*s'\n", (int)sizeof(header->git_version), header->git_version);
    debug_log("  firmware_size : %u\n", header->firmware_size);
    debug_log("  slot          : %u\n", header->slot);
    debug_log("  crc32         : 0x%08X\n", header->crc32);
    debug_log("  meta_version  : %u\n", header->meta_version);
    debug_log("  use_case_id   : %u\n", header->use_case_id);
    debug_log("  use_case_name : '%.*s'\n", (int)sizeof(header->use_case_name),
              header->use_case_name);
    if (firmware_header_use_case_meta_valid(header)) {
        const char *expected_name = use_case_name_from_id(header->use_case_id);
        debug_log("  use_case_meta : valid (%s/%u)\n", expected_name ? expected_name : "unknown",
                  (unsigned)header->use_case_id);
    } else if (header->meta_version == 0 && header->use_case_id == 0 &&
               header->use_case_name[0] == '\0') {
        debug_log("  use_case_meta : legacy/unknown\n");
    } else {
        debug_log("  use_case_meta : invalid\n");
    }

    header->valid_flag = 1;

    // Erase and reprogram the 4 KB flash sector
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);
    flash_range_program(flash_offset, sector_buffer, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);

    debug_log("Firmware marked valid (sector-based rewrite).\n");
}

static bool demote_firmware_header_priority(uint32_t header_flash_offset) {
    const uint32_t sector_base = header_flash_offset & ~(FLASH_SECTOR_SIZE - 1);
    const size_t header_offset_in_sector = (size_t)(header_flash_offset - sector_base);
    if (header_offset_in_sector + sizeof(firmware_header_t) > FLASH_SECTOR_SIZE) {
        debug_log_with_color(COLOR_RED, "DEMOTE SLOT: header crosses sector boundary at 0x%08X\n",
                             header_flash_offset);
        return false;
    }

    uint8_t sector_buffer[FLASH_SECTOR_SIZE];
    memcpy(sector_buffer, FLASH_PTR(sector_base), FLASH_SECTOR_SIZE);

    firmware_header_t *header = (firmware_header_t *)(sector_buffer + header_offset_in_sector);
    if (memcmp(header->magic, FIRMWARE_MAGIC, FIRMWARE_MAGIC_LEN) != 0 || header->valid_flag != 1) {
        debug_log_with_color(COLOR_RED,
                             "DEMOTE SLOT: invalid header at 0x%08X (magic/valid mismatch)\n",
                             header_flash_offset);
        return false;
    }

    debug_log("DEMOTE SLOT: before slot=%u version='%.*s' build='%.*s'\n", (unsigned)header->slot,
              (int)sizeof(header->git_version), header->git_version,
              (int)sizeof(header->build_date), header->build_date);

    memset(header->git_version, 0, sizeof(header->git_version));
    memset(header->build_date, 0, sizeof(header->build_date));
    // Keep a parseable semantic version so bootloader compare_versions() remains deterministic.
    snprintf(header->git_version, sizeof(header->git_version), "v0.0.0-0");
    snprintf(header->build_date, sizeof(header->build_date), "1970-01-01");

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sector_base, FLASH_SECTOR_SIZE);
    flash_range_program(sector_base, sector_buffer, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);

    debug_log(
        "DEMOTE SLOT: after  slot=%u version='%.*s' build='%.*s' (header=0x%08X sector=0x%08X)\n",
        (unsigned)header->slot, (int)sizeof(header->git_version), header->git_version,
        (int)sizeof(header->build_date), header->build_date, header_flash_offset, sector_base);
    return true;
}

static void flush_page_to_flash(void) {
    if (flash_writer.buffer_filled == 0) {
        debug_log("FLASH: flush_page_to_flash() called, but buffer is empty – skipping\n");
        return;
    }
    watchdog_update();

    // Padding if needed
    if (flash_writer.buffer_filled % FLASH_PAGE_SIZE != 0) {
        size_t pad_size = FLASH_PAGE_SIZE - flash_writer.buffer_filled;
        memset(flash_writer.buffer + flash_writer.buffer_filled, 0xFF, pad_size);
        debug_log("FLASH: padding %u bytes with 0xFF\n", (unsigned)pad_size);
    }

    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(flash_writer.flash_offset, flash_writer.buffer, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    flash_writer.flash_offset += FLASH_PAGE_SIZE;
    flash_writer.buffer_filled = 0;
}
