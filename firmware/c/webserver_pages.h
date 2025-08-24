/*
 * ==============================================================================
 * webserver_pages.h - HTML Page Generation for inki Webserver
 * ==============================================================================
 *
 * Contains all HTML page generation functions extracted from webserver.c
 * These functions generate complete HTML responses for the web configuration
 * interface.
 *
 */

#ifndef WEBSERVER_PAGES_H
#define WEBSERVER_PAGES_H

#include "lwip/tcp.h"

// HTML page generation functions
void send_device_status_page(struct tcp_pcb* tpcb);
void send_upload_logo_page(struct tcp_pcb* tpcb, const char* message);
void send_firmware_update_page(struct tcp_pcb* tpcb, const char* message);
void send_landing_page(struct tcp_pcb *tpcb);
void send_wifi_config_page(struct tcp_pcb *tpcb, const char *message);
void send_seatsurfing_config_page(struct tcp_pcb* tpcb, const char* message);
void send_clock_page(struct tcp_pcb *tpcb, const char *message);
void send_device_config_page(struct tcp_pcb* tpcb, const char* message);

#endif // WEBSERVER_PAGES_H