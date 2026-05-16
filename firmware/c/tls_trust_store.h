#pragma once

#include <stddef.h>
#include <stdint.h>

struct altcp_tls_config;
struct altcp_pcb;

// Initialize TLS client config. Must be called after Wi-Fi initialization.
void tls_init(void);

// Get global TLS client config (NULL if init failed or TLS not enabled for this use case).
struct altcp_tls_config *tls_get_client_config(void);

// Set TLS SNI hostname on an established PCB. No-op if pcb or hostname is NULL/empty.
void tls_apply_sni(struct altcp_pcb *pcb, const char *hostname);
