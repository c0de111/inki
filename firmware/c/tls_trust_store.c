#include "tls_trust_store.h"
#include "config.h"

#ifdef USE_CASE_WEATHERMAP

#define LOG_MODULE LOG_MOD_TLS
#include "debug.h"
#include "lwip/altcp_tls.h"
#if INKI_DEBUG_TLS_CB
#include "mbedtls/debug.h"
#endif
#include <stdlib.h>
#include <string.h>

// Trust store selection
// Prefer a consolidated PEM trust bundle if available (can include multiple CAs such as HARICA,
// ISRG Root X1, etc.). Fallback to legacy HARICA bundle if present. Otherwise, validation is
// disabled.

#if __has_include("certs/trust_store_pem.h")
#include "certs/trust_store_pem.h" // defines: trust_store_pem, trust_store_pem_len (bytes)
#define HAVE_TRUST_PEM 1
#elif __has_include("certs/harica_bundle.h")
#include "certs/harica_bundle.h" // defines: harica_bundle_der, harica_bundle_der_len (bytes)
#define HAVE_HARICA_DER 1
#else
#define HAVE_TRUST_PEM 0
#define HAVE_HARICA_DER 0
static const uint8_t empty_ca[] = {};
static const size_t empty_ca_len = 0;
#endif

static struct altcp_tls_config *s_tls_cfg = NULL;

#if INKI_DEBUG_TLS_CB
static void tls_enable_debug_callback(void) {
    // ALTCP mbedTLS config already installs a debug callback internally.
    // Raising threshold here enables verbose TLS diagnostics.
    mbedtls_debug_set_threshold(4);
    debug_log_with_color(COLOR_BOLD_YELLOW,
                         "[TLS] INKI_DEBUG_TLS_CB enabled (mbedTLS debug threshold=4)\n");
}
#endif

void tls_trust_store_init(void) {
    if (s_tls_cfg)
        return;

#if HAVE_TRUST_PEM
    debug_log("[TLS] Trust store (PEM) available (%u bytes)\n", (unsigned)trust_store_pem_len);
#elif HAVE_HARICA_DER
    debug_log("[TLS] HARICA certificate bundle (DER) available (%u bytes)\n",
              (unsigned)harica_bundle_der_len);
#else
    debug_log_with_color(
        COLOR_YELLOW, "[TLS] No certificate bundle embedded - certificate validation disabled\n");
#endif

    dtrace("[TLS] Trust store initialized (config deferred until after Wi-Fi init)\n");
}

void tls_create_config_after_wifi(void) {
    if (s_tls_cfg)
        return;

    // Create TLS config AFTER Wi-Fi initialization (like pico-examples)
    dtrace("[TLS] Creating ALTCP TLS client config after Wi-Fi init\n");

// Use certificate validation if available
#if HAVE_TRUST_PEM
    // Ensure PEM is NUL-terminated for parsers that expect it
    const u8_t *ca_ptr = (const u8_t *)trust_store_pem;
    size_t ca_len = (size_t)trust_store_pem_len;
    u8_t *tmp_buf = NULL;
    if (ca_len == 0 || trust_store_pem[ca_len - 1] != '\0') {
        tmp_buf = (u8_t *)malloc(ca_len + 1);
        if (tmp_buf) {
            memcpy(tmp_buf, trust_store_pem, ca_len);
            tmp_buf[ca_len] = '\0';
            ca_ptr = tmp_buf;
            ca_len = ca_len + 1;
        }
    }
    s_tls_cfg = altcp_tls_create_config_client(ca_ptr, ca_len);
    if (tmp_buf) {
        free(tmp_buf);
        tmp_buf = NULL;
    }
    if (!s_tls_cfg) {
        debug_log_with_color(
            COLOR_YELLOW,
            "[TLS] PEM trust store parse failed; attempting fallback CA bundle (if present)\n");
    }
#endif
#if !HAVE_TRUST_PEM || (HAVE_TRUST_PEM && 0)
    /* nothing */
#endif
#if HAVE_HARICA_DER
    if (!s_tls_cfg) {
        s_tls_cfg = altcp_tls_create_config_client(harica_bundle_der, harica_bundle_der_len);
    }
#endif
#if !HAVE_TRUST_PEM && !HAVE_HARICA_DER
    if (!s_tls_cfg) {
        s_tls_cfg = altcp_tls_create_config_client(NULL, 0);
        debug_log_with_color(COLOR_YELLOW,
                             "[TLS] No certificate validation (fallback for testing)\n");
    }
#endif

    if (!s_tls_cfg) {
        debug_log_with_color(COLOR_RED, "[TLS] Failed to create ALTCP TLS client config\n");
        return;
    }

#if INKI_DEBUG_TLS_CB
    tls_enable_debug_callback();
#endif

    debug_log_with_color(COLOR_GREEN,
                         "[TLS] ALTCP TLS config created successfully after Wi-Fi init\n");
}

struct altcp_tls_config *tls_get_client_config(void) { return s_tls_cfg; }

#else // !USE_CASE_WEATHERMAP

// Stub implementations for non-weathermap use cases
void tls_trust_store_init(void) {
    // No-op for use cases that don't need TLS
}

void tls_create_config_after_wifi(void) {
    // No-op for use cases that don't need TLS
}

struct altcp_tls_config *tls_get_client_config(void) {
    return NULL; // No TLS config for non-weathermap use cases
}

#endif // USE_CASE_WEATHERMAP
