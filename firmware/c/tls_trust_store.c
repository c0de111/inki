#include "tls_trust_store.h"
#include "config.h"

#ifdef TRUST_STORE_HEADER

#define LOG_MODULE LOG_MOD_TLS
#include "debug.h"
#include "lwip/altcp_tls.h"
#include "mbedtls/ssl.h"
#if INKI_DEBUG_TLS_CB
#include "mbedtls/debug.h"
#endif

#include TRUST_STORE_HEADER // defines: trust_store_pem[], trust_store_pem_len (includes NUL)

static struct altcp_tls_config *s_tls_cfg = NULL;

#if INKI_DEBUG_TLS_CB
static void tls_enable_debug_callback(void) {
    mbedtls_debug_set_threshold(4);
    dlog("[TLS] INKI_DEBUG_TLS_CB enabled (mbedTLS debug threshold=4)\n");
}
#endif

void tls_init(void) {
    if (s_tls_cfg)
        return;

    dlog("[TLS] Trust store: %u bytes\n", (unsigned)trust_store_pem_len);

    s_tls_cfg =
        altcp_tls_create_config_client((const u8_t *)trust_store_pem, (size_t)trust_store_pem_len);

    if (!s_tls_cfg) {
        dlog("[TLS] Failed to create ALTCP TLS client config\n");
        return;
    }

#if INKI_DEBUG_TLS_CB
    tls_enable_debug_callback();
#endif

    dlog("[TLS] ALTCP TLS config created successfully\n");
}

struct altcp_tls_config *tls_get_client_config(void) { return s_tls_cfg; }

void tls_apply_sni(struct altcp_pcb *pcb, const char *hostname) {
    if (!pcb || !hostname || hostname[0] == '\0')
        return;
    void *ctx = altcp_tls_context(pcb);
    if (ctx)
        mbedtls_ssl_set_hostname((mbedtls_ssl_context *)ctx, hostname);
}

#else // !TRUST_STORE_HEADER — stubs for use cases that don't need TLS

void tls_init(void) {}
struct altcp_tls_config *tls_get_client_config(void) { return NULL; }
void tls_apply_sni(struct altcp_pcb *pcb, const char *hostname) {
    (void)pcb;
    (void)hostname;
}

#endif // TRUST_STORE_HEADER
