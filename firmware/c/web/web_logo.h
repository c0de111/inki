#ifndef WEB_LOGO_H
#define WEB_LOGO_H

#include "lwip/tcp.h"
#include <stddef.h>

// Route handlers (registered in webserver.c route table)
void web_serve_logo(struct tcp_pcb *tpcb);
void web_delete_logo(struct tcp_pcb *tpcb);
void web_logo_upload(struct tcp_pcb *tpcb, int content_length, const char *body, size_t body_len);

#endif // WEB_LOGO_H
