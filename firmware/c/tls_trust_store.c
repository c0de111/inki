#include "tls_trust_store.h"
#include "config.h"

#ifdef USE_CASE_WEATHERMAP

#include "lwip/altcp_tls.h"
#include "debug.h"
#include "mbedtls/debug.h"

// HARICA certificate bundle (intermediate + root CA) - required to verify *.geodatenzentrum.de
// If the header is present, we include the real DER bytes.
// Otherwise, we fall back to an empty trust store (verification disabled) for bring-up.
#if __has_include("certs/harica_bundle.h")
#include "certs/harica_bundle.h"
#else
static const uint8_t harica_bundle_der[] = { };
static const size_t harica_bundle_der_len = 0;
#endif

static struct altcp_tls_config* s_tls_cfg = NULL;

// mbedTLS debug callback for TLS troubleshooting
static void tls_debug_callback(void *ctx, int level, const char *file, int line, const char *str) {
    debug_log("[TLS_DEBUG] %s:%d: %s", file, line, str);
}

void tls_trust_store_init(void) {
    if (s_tls_cfg) return;

    if (harica_bundle_der_len == 0) {
        debug_log_with_color(COLOR_YELLOW, "[TLS] No HARICA certificate bundle embedded - using no certificate validation\n");
    } else {
        debug_log("[TLS] HARICA certificate bundle available (%u bytes)\n", (unsigned)harica_bundle_der_len);
    }
    
    #ifdef HIGH_VERBOSE_DEBUG
    debug_log("[TLS] TLS trust store initialized (config creation deferred until after Wi-Fi init)\n");
    #endif
}

void tls_create_config_after_wifi(void) {
    if (s_tls_cfg) return;
    
    // Create TLS config AFTER Wi-Fi initialization (like pico-examples)
    #ifdef HIGH_VERBOSE_DEBUG
    debug_log("[TLS] Creating ALTCP TLS client config after Wi-Fi init (pico-examples pattern)\n");
    #endif
    
    // Use certificate validation if available
    if (harica_bundle_der_len > 0) {
        s_tls_cfg = altcp_tls_create_config_client(harica_bundle_der, harica_bundle_der_len);
        #ifdef HIGH_VERBOSE_DEBUG
        debug_log("[TLS] Using HARICA certificate bundle for certificate validation\n");
        #endif
    } else {
        s_tls_cfg = altcp_tls_create_config_client(NULL, 0);
        debug_log_with_color(COLOR_YELLOW, "[TLS] No certificate validation (fallback for testing)\n");
    }
    
    if (!s_tls_cfg) {
        debug_log_with_color(COLOR_RED, "[TLS] Failed to create ALTCP TLS client config\n");
        return;
    }
    
    debug_log_with_color(COLOR_GREEN, "[TLS] ALTCP TLS config created successfully after Wi-Fi init\n");
}

struct altcp_tls_config* tls_get_client_config(void) {
    return s_tls_cfg;
}

#else // !USE_CASE_WEATHERMAP

// Stub implementations for non-weathermap use cases
void tls_trust_store_init(void) {
    // No-op for use cases that don't need TLS
}

void tls_create_config_after_wifi(void) {
    // No-op for use cases that don't need TLS
}

struct altcp_tls_config* tls_get_client_config(void) {
    return NULL; // No TLS config for non-weathermap use cases
}

#endif // USE_CASE_WEATHERMAP
