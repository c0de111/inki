// Minimal TLS trust store interface for ALTCP + mbedTLS
#pragma once

#include <stddef.h>
#include <stdint.h>

struct altcp_tls_config;

// Initialize global TLS client config (idempotent)
void tls_trust_store_init(void);

// Create TLS config after Wi-Fi initialization (call after wifi_connect)
void tls_create_config_after_wifi(void);

// Get global TLS client config (NULL if init failed)
struct altcp_tls_config *tls_get_client_config(void);
