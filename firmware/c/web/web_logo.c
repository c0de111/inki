#include "web_logo.h"

#define LOG_MODULE LOG_MOD_WEBSERVER
#include "debug.h"
#include "flash.h"
#include "hardware/flash.h"
#include "ota.h"
#include "web_firmware.h"
#include "webserver.h"
#include "webserver_scaffold.h"
#include "webserver_utils.h"

#include <stdlib.h>
#include <string.h>

// =============================================================================
// Logo serve — read 1-bit flash image, encode as BMP, send
// =============================================================================

void web_serve_logo(struct tcp_pcb *tpcb) {
    const logo_header_t *hdr = (const logo_header_t *)FLASH_PTR(LOGO_FLASH_OFFSET);
    if (memcmp(hdr->magic, "LOGO", 4) != 0 || hdr->width == 0 || hdr->height == 0) {
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
    int row_bytes = (width + 7) / 8;
    int row_padded = (row_bytes + 3) & ~3;
    int pixel_array_size = row_padded * height;
    int file_size = 14 + 40 + 8 + pixel_array_size;
    int offset_pixels = 14 + 40 + 8;

    uint8_t *bmp = (uint8_t *)malloc(file_size);
    if (!bmp) {
        send_response(tpcb, "<html><body><h3>OOM logo</h3></body></html>");
        return;
    }

    // BITMAPFILEHEADER (14 bytes)
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
    d += 4;
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

    // Color table (black=index 0, white=index 1) in BGRA
    *d++ = 0x00;
    *d++ = 0x00;
    *d++ = 0x00;
    *d++ = 0x00; // black
    *d++ = 0xFF;
    *d++ = 0xFF;
    *d++ = 0xFF;
    *d++ = 0x00; // white

    // Pixel data: bottom-up, left-to-right, MSB first, rows padded to 4 bytes
    // Flash stores bit=1 as black; BMP palette has index 0=black, so invert.
    uint8_t *pix = bmp + offset_pixels;
    for (int y = 0; y < height; y++) {
        const uint8_t *src_row = src + ((height - 1 - y) * row_bytes);
        int out_byte = 0, bitpos = 7;
        for (int x = 0; x < width; x++) {
            int bit = (src_row[x / 8] >> (7 - (x % 8))) & 1; // 1=black in flash
            out_byte |= ((bit ? 0 : 1) << bitpos);           // invert: 0=black in BMP
            if (bitpos == 0) {
                *pix++ = (uint8_t)out_byte;
                out_byte = 0;
                bitpos = 7;
            } else {
                bitpos--;
            }
        }
        if (bitpos != 7)
            *pix++ = (uint8_t)out_byte;
        int written = row_bytes;
        while (written < row_padded) {
            *pix++ = 0x00;
            written++;
        }
    }

    send_binary_response(tpcb, "image/bmp", bmp, file_size);
    free(bmp);
}

// =============================================================================
// Logo delete
// =============================================================================

void web_delete_logo(struct tcp_pcb *tpcb) {
    flash_erase_logo();
    dlog("UPLOAD: logo flash erased\n");
    upload_session.active = false;
    upload_session.header_complete = false;
    upload_session.header_length = 0;
    char *buf = malloc(SCAFFOLD_PAGE_SIZE);
    if (!buf) {
        send_response(tpcb, "<h2>Out of memory</h2>");
        return;
    }
    build_upload_logo_page(buf, SCAFFOLD_PAGE_SIZE, "✔️ Logo successfully deleted.");
    send_response(tpcb, buf);
    free(buf);
}

// =============================================================================
// Logo upload — OTA completion callback + ROUTE_BINARY handler
// =============================================================================

static void logo_upload_complete(struct tcp_pcb *tpcb, size_t total_written) {
    (void)total_written;
    debug_status("OK", "LOGO: complete (%d bytes)\n", (int)total_written);
    char *buf = malloc(SCAFFOLD_PAGE_SIZE);
    if (!buf) {
        send_response(tpcb, "<h2>Out of memory</h2>");
        return;
    }
    build_upload_logo_page(buf, SCAFFOLD_PAGE_SIZE, "✅ Logo uploaded successfully.");
    send_response(tpcb, buf);
    free(buf);
}

void web_logo_upload(struct tcp_pcb *tpcb, int content_length, const char *body, size_t body_len) {
    if (content_length < 0) {
        dlog("UPLOAD LOGO: Content-Length missing\n");
        send_response(tpcb, "<h2 style='color:red'>Missing Content-Length</h2>");
        reset_upload_session();
        tcp_close(tpcb);
        return;
    }
    if ((size_t)content_length > LOGO_FLASH_SIZE) {
        dlog("UPLOAD LOGO: File too large (%d > %d bytes)\n", content_length, LOGO_FLASH_SIZE);
        send_response(tpcb, "<h2 style='color:red'>Logo too large</h2>");
        reset_upload_session();
        tcp_close(tpcb);
        return;
    }

    dlog("UPLOAD LOGO: Expected length: %d\n", content_length);
    upload_session.expected_length = (size_t)content_length;
    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_LOGO;
    upload_session.flash_offset = LOGO_FLASH_OFFSET;

    size_t erase_length =
        ((size_t)content_length + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
    ota_begin(tpcb, (size_t)content_length, erase_length, LOGO_FLASH_OFFSET, logo_upload_complete);

    if (body && body_len > 0) {
        if (body_len > (size_t)content_length)
            body_len = (size_t)content_length;
        ota_stage_data(body, body_len);
    }
}
