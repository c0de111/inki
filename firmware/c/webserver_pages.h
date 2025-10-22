/*
 * ==============================================================================
 * webserver_pages.h - HTML Page Generation & Form Processing for inki Webserver
 * ==============================================================================
 *
 * Function declarations for HTML page generation and form processing handlers
 * extracted from webserver.c. This module provides:
 * 
 * - HTML page generation functions for the web configuration interface
 * - Form data processing handlers for configuration updates
 * - Integration with flash memory storage and user feedback
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
#ifdef USE_CASE_SEATSURFING
void send_seatsurfing_config_page(struct tcp_pcb* tpcb, const char* message);
#elif defined(USE_CASE_HISTORIAN)
void send_historian_config_page(struct tcp_pcb* tpcb, const char* message);
#elif defined(USE_CASE_HOMEMATIC)
void send_homematic_config_page(struct tcp_pcb* tpcb, const char* message);
#elif defined(USE_CASE_WEATHERMAP)
void send_weathermap_page(struct tcp_pcb* tpcb, const char* message);
#endif
void send_clock_page(struct tcp_pcb *tpcb, const char *message);
void send_device_config_page(struct tcp_pcb* tpcb, const char* message);
void send_message_page(struct tcp_pcb* tpcb, const char* message);

// Form handler functions
void handle_form_wifi(struct tcp_pcb *tpcb, const char *body, size_t len);
#ifdef USE_CASE_SEATSURFING
void handle_form_seatsurfing(struct tcp_pcb *tpcb, const char *body, size_t len);
#elif defined(USE_CASE_HISTORIAN)
void handle_form_historian(struct tcp_pcb *tpcb, const char *body, size_t len);
#elif defined(USE_CASE_HOMEMATIC)
void handle_form_homematic(struct tcp_pcb *tpcb, const char *body, size_t len);
#elif defined(USE_CASE_WEATHERMAP)
void handle_form_weathermap(struct tcp_pcb *tpcb, const char *body, size_t len);
#endif
void handle_form_device_config(struct tcp_pcb *tpcb, const char *body, size_t len);
void handle_form_clock(struct tcp_pcb *tpcb, const char *body, size_t len);
void handle_form_message(struct tcp_pcb *tpcb, const char *body, size_t len);

#endif // WEBSERVER_PAGES_H
