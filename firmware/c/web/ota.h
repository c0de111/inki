#ifndef OTA_H
#define OTA_H

#include "lwip/tcp.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Called by webserver_ota_tick() when all bytes are written to flash.
// tpcb: connection to send the completion response on.
// total_written: total bytes flushed (== expected if no truncation).
typedef void (*ota_complete_fn)(struct tcp_pcb *tpcb, size_t total_written);

// Begin a binary upload: erase target region, reset ring buffer.
// on_complete is called once from OTA_DONE (Thread mode, not recv_cb).
void ota_begin(struct tcp_pcb *tpcb, size_t expected, size_t erase_length, uint32_t slot_offset,
               ota_complete_fn on_complete);

void ota_stage_data(const char *buffer, size_t len);
bool ota_is_idle(void);
void webserver_ota_tick(void);

#endif
