#include "ota.h"

#define LOG_MODULE LOG_MOD_WEBSERVER
#include "debug.h"
#include "flash.h"
#include "hardware/flash.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "webserver.h"
#include "webserver_utils.h"

#include <string.h>

// =============================================================================
// OTA STATE MACHINE
// recv_cb stages data here; webserver_run()'s event loop flushes to flash.
// sleep_ms() is only effective in Thread mode (not inside a CYW43 callback).
// =============================================================================

#define OTA_STAGING_SIZE (12 * 1024) // must be >= TCP_WND = 8*TCP_MSS = 11,680

typedef enum { OTA_IDLE, OTA_ERASING, OTA_WRITING, OTA_DONE } ota_state_t;

typedef struct {
    ota_state_t state;
    uint8_t staging[OTA_STAGING_SIZE]; // ring buffer
    size_t write_head;                 // bytes written by recv_cb   (ever-increasing)
    size_t flush_head;                 // bytes flushed by event loop (ever-increasing)
    size_t expected;                   // total bytes (from Content-Length)
    uint32_t slot_offset;              // target flash slot base address
    struct tcp_pcb *tpcb;
    int progress_pct;
    ota_complete_fn on_complete;
} ota_t;

static ota_t ota;

void ota_begin(struct tcp_pcb *tpcb, size_t expected, size_t erase_length, uint32_t slot_offset,
               ota_complete_fn on_complete) {
    flash_ota_begin(slot_offset, erase_length);
    memset(&ota, 0, sizeof(ota));
    ota.expected = expected;
    ota.slot_offset = slot_offset;
    ota.tpcb = tpcb;
    ota.on_complete = on_complete;
    ota.state = OTA_ERASING;
}

void ota_stage_data(const char *buffer, size_t len) {
    size_t avail = OTA_STAGING_SIZE - (ota.write_head - ota.flush_head);
    if (len > avail)
        len = avail;
    size_t pos = ota.write_head % OTA_STAGING_SIZE;
    size_t first = OTA_STAGING_SIZE - pos;
    if (len <= first) {
        memcpy(ota.staging + pos, buffer, len);
    } else {
        memcpy(ota.staging + pos, buffer, first);
        memcpy(ota.staging, buffer + first, len - first);
    }
    ota.write_head += len;
}

bool ota_is_idle(void) { return ota.state == OTA_IDLE; }

static void ota_staging_read_page(uint8_t *dst) {
    size_t pos = ota.flush_head % OTA_STAGING_SIZE;
    size_t first = OTA_STAGING_SIZE - pos;
    if (first >= FLASH_PAGE_SIZE) {
        memcpy(dst, ota.staging + pos, FLASH_PAGE_SIZE);
    } else {
        memcpy(dst, ota.staging + pos, first);
        memcpy(dst + first, ota.staging, FLASH_PAGE_SIZE - first);
    }
}

void webserver_ota_tick(void) {
    switch (ota.state) {
    case OTA_IDLE:
        return;

    case OTA_ERASING:
        if (flash_ota_erase_next_sector()) {
            ota.state = OTA_WRITING;
            debug_status("OK", "UPLOAD: erase done, receiving data\n");
        }
        sleep_ms(5);
        break;

    case OTA_WRITING: {
        size_t pending = ota.write_head - ota.flush_head;
        bool all_received = (ota.write_head >= ota.expected);

        if (pending >= FLASH_PAGE_SIZE) {
            uint8_t page[FLASH_PAGE_SIZE];
            ota_staging_read_page(page);
            flash_ota_write_page(page);
            ota.flush_head += FLASH_PAGE_SIZE;

            cyw43_arch_lwip_begin();
            tcp_recved(ota.tpcb, FLASH_PAGE_SIZE);
            cyw43_arch_lwip_end();

            sleep_ms(1);

            if (ota.expected > 0) {
                int pct = (int)((100ULL * ota.flush_head) / ota.expected);
                if (pct >= ota.progress_pct + 10) {
                    ota.progress_pct = pct;
                    debug_status("OK", "UPLOAD: %d%%\n", pct);
                }
            }
        } else if (all_received) {
            if (pending > 0) {
                uint8_t page[FLASH_PAGE_SIZE];
                memset(page, 0xFF, FLASH_PAGE_SIZE);
                size_t pos = ota.flush_head % OTA_STAGING_SIZE;
                size_t first = OTA_STAGING_SIZE - pos;
                if (first >= pending) {
                    memcpy(page, ota.staging + pos, pending);
                } else {
                    memcpy(page, ota.staging + pos, first);
                    memcpy(page + first, ota.staging, pending - first);
                }
                flash_ota_write_page(page);
                ota.flush_head += pending;

                cyw43_arch_lwip_begin();
                tcp_recved(ota.tpcb, (u16_t)pending);
                cyw43_arch_lwip_end();
            }
            ota.state = OTA_DONE;
        }
        break;
    }

    case OTA_DONE:
        if (ota.on_complete)
            ota.on_complete(ota.tpcb, ota.expected);
        reset_upload_session();
        upload_session.active = false;
        upload_session.header_complete = false;
        upload_session.header_length = 0;
        ota.state = OTA_IDLE;
        break;
    }
}
