#ifndef WEB_FIRMWARE_H
#define WEB_FIRMWARE_H

#include "lwip/tcp.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Page builders (also called by web_logo.c)
void build_firmware_update_page(char *buf, size_t sz, const char *message);
void build_upload_logo_page(char *buf, size_t sz, const char *message);

// Shared firmware slot helper (used by web_status.c)
void format_slot_use_case(uint8_t slot, char *out, size_t out_len, bool with_id);

// Route handlers (registered in webserver.c route table)
void web_firmware_upload(struct tcp_pcb *tpcb, int content_length, const char *body,
                         size_t body_len);
void web_firmware_demote_active(struct tcp_pcb *tpcb);

#endif // WEB_FIRMWARE_H
