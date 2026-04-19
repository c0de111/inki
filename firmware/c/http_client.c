#include "http_client.h"
#define LOG_MODULE LOG_MOD_HTTP
#include "debug.h"

#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "lwip/altcp_tls.h"
#include "mbedtls/ssl.h"
#include "tls_trust_store.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static http_session_t g_session = {0};
static int s_body_progress_pct = -10; // reset per-request; avoids static-local persistence bug
static int g_last_status_code = 0;    // last HTTP status code received; 0 = no response yet

// Server time extracted from HTTP Date header (UTC)
static rtc_time_t g_server_time = {0};
static bool g_server_time_valid = false;

// === Helper Functions ===

// Frees body/request buffers and zeros all session state. Safe to call multiple times.
static void reset_session(http_session_t *session) {
    session->active = false;
    session->state = HTTP_SESSION_INACTIVE;
    session->header_complete = false;
    session->transfer_complete = false;
    session->fallback_mode = false;
    session->no_store_body = false;
    session->stream_mode = false;
    session->use_tls = false;
    session->header_length = 0;
    session->expected_length = 0;
    session->total_received = 0;
    session->last_error = ERR_OK;
    session->transfer_was_successful = false;
    g_last_status_code = 0;
    session->completion_callback = NULL;
    session->callback_arg = NULL;

    if (session->body_buffer) {
        free(session->body_buffer);
        session->body_buffer = NULL;
    }
    session->body_buffer_size = 0;

    if (session->request_data) {
        free(session->request_data);
        session->request_data = NULL;
    }
    session->request_length = 0;

    session->pcb = NULL;
    session->server_hostname[0] = '\0';

    session->stream_on_header = NULL;
    session->stream_on_data = NULL;
    session->stream_on_complete = NULL;
    session->stream_arg = NULL;
}

static int parse_content_length(const char *header) {
    const char *cl = strstr(header, "Content-Length:");
    if (!cl) {
        cl = strstr(header, "content-length:"); // Case-insensitive
    }
    if (!cl)
        return -1;

    cl += 15; // Skip "Content-Length:"
    while (*cl == ' ')
        cl++; // Skip spaces

    return atoi(cl);
}

static int parse_http_status_code(const char *header) {
    const char *p = strstr(header, "HTTP/");
    if (!p)
        return -1;
    // Find first space after HTTP/version
    p = strchr(p, ' ');
    if (!p)
        return -1;
    // Skip spaces and parse number
    while (*p == ' ')
        p++;
    return atoi(p);
}

// Extract hostname from the HTTP request's Host header (without optional :port)
static void extract_host_from_request(const char *request, char *out_host, size_t out_size) {
    if (!request || !out_host || out_size == 0)
        return;
    out_host[0] = '\0';
    const char *line = request;
    while (line) {
        if (strncasecmp(line, "Host:", 5) == 0) {
            const char *v = line + 5;
            while (*v == ' ' || *v == '\t')
                v++;
            const char *end = v;
            while (*end && *end != '\r' && *end != '\n')
                end++;
            size_t len = (size_t)(end - v);
            if (len >= out_size)
                len = out_size - 1;
            size_t copy = len;
            for (size_t i = 0; i < len; i++) {
                if (v[i] == ':') {
                    copy = i;
                    break;
                }
            }
            memcpy(out_host, v, copy);
            out_host[copy] = '\0';
            return;
        }
        const char *nl = strchr(line, '\n');
        line = nl ? nl + 1 : NULL;
    }
}

// === TCP Callbacks ===

static void handle_connection_closed(http_session_t *session, struct altcp_pcb *pcb) {
    dlog("[HTTP] Connection closed by server\n");
    altcp_err(pcb, NULL);
    altcp_recv(pcb, NULL);
    altcp_close(pcb);
    session->pcb = NULL;

    // Fallback mode: connection close marks end of body
    if (session->active && !session->transfer_complete && session->header_complete &&
        session->fallback_mode) {
        session->transfer_complete = (session->total_received > 0);
        if (session->transfer_complete) {
            if (!session->no_store_body && session->body_buffer) {
                session->body_buffer[session->total_received] = '\0';
            }
            session->transfer_was_successful = true;
            if (session->completion_callback) {
                session->completion_callback(session->no_store_body ? NULL : session->body_buffer,
                                             session->total_received, true, session->callback_arg);
            } else if (session->stream_mode && session->stream_on_complete) {
                session->stream_on_complete(true, session->stream_arg);
            }
            reset_session(session);
            return;
        }
    }

    if (session->active && !session->transfer_complete) {
        if (session->completion_callback) {
            session->completion_callback(NULL, 0, false, session->callback_arg);
        } else if (session->stream_mode && session->stream_on_complete) {
            session->stream_on_complete(false, session->stream_arg);
        }
    }
    reset_session(session);
}

// Deregister callbacks, release pcb/pbuf, fire failure notification, reset session.
// Returns true so callers can write: return fail_session(session, pcb, p, ERR_xxx);
static bool fail_session(http_session_t *session, struct altcp_pcb *pcb, struct pbuf *p,
                         err_t err_code) {
    altcp_err(pcb, NULL);
    altcp_recv(pcb, NULL);
    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    altcp_close(pcb);
    session->state = HTTP_SESSION_ERROR;
    session->last_error = err_code;
    if (session->completion_callback)
        session->completion_callback(NULL, 0, false, session->callback_arg);
    else if (session->stream_mode && session->stream_on_complete)
        session->stream_on_complete(false, session->stream_arg);
    reset_session(session);
    return true;
}

// Returns true if caller should return ERR_OK immediately (pcb/pbuf already handled inside).
static bool recv_header_phase(http_session_t *session, struct altcp_pcb *pcb, struct pbuf *p,
                              const char *chunk, int step) {
    size_t space = sizeof(session->header_buffer) - session->header_length - 1;
    size_t to_copy = ((size_t)step < space) ? (size_t)step : space;

    if (to_copy > 0) {
        memcpy(session->header_buffer + session->header_length, chunk, to_copy);
        session->header_length += to_copy;
        session->header_buffer[session->header_length] = '\0';
    }
    dtrace("[HTTP] Header chunk: %d bytes (total: %d)\n", step, (int)session->header_length);

    if (space < (size_t)step && !strstr(session->header_buffer, "\r\n\r\n")) {
        dlog("[HTTP] Header too large for buffer\n");
        return fail_session(session, pcb, p, ERR_BUF);
    }

    char *header_end = strstr(session->header_buffer, "\r\n\r\n");
    if (!header_end)
        return false; // still accumulating

    session->header_complete = true;
    session->state = HTTP_SESSION_RECEIVING_BODY;
    header_end += 4;

    g_server_time_valid = rtc_parse_http_date(session->header_buffer, &g_server_time);

    int status = parse_http_status_code(session->header_buffer);
    g_last_status_code = (status >= 0) ? status : 0;
    if (status >= 0 && (status < 200 || status >= 300)) {
        char status_line[80] = {0};
        const char *nl = strchr(session->header_buffer, '\n');
        size_t sl_len = nl ? (size_t)(nl - session->header_buffer) : 79;
        if (sl_len > 79)
            sl_len = 79;
        memcpy(status_line, session->header_buffer, sl_len);
        debug_status("ERROR", "[HTTP] Error status: %d — %s\n", status, status_line);
        return fail_session(session, pcb, p, ERR_CLSD);
    }

    const char *te1 = strstr(session->header_buffer, "Transfer-Encoding:");
    const char *te2 = te1 ? te1 : strstr(session->header_buffer, "transfer-encoding:");
    if (te2 && (strstr(te2, "chunked") || strstr(te2, "Chunked"))) {
        dlog("[HTTP] Chunked transfer-encoding not supported\n");
        return fail_session(session, pcb, p, ERR_VAL);
    }

    int content_length = parse_content_length(session->header_buffer);
    if (session->stream_on_header && session->no_store_body) {
        session->stream_on_header(session->header_buffer, session->header_length, status,
                                  content_length, session->stream_arg);
    }

    if (content_length < 0) {
        session->fallback_mode = true;
        session->expected_length = 0;
        session->body_buffer = NULL;
        session->body_buffer_size = 0;
        session->total_received = 0;
        size_t body_in_header =
            session->header_length - (size_t)(header_end - session->header_buffer);
        if (body_in_header > 0) {
            if (session->no_store_body && session->stream_on_data) {
                session->stream_on_data((const uint8_t *)header_end, body_in_header,
                                        session->stream_arg);
                session->total_received = body_in_header;
            } else {
                session->body_buffer = (char *)malloc(body_in_header + 1);
                if (!session->body_buffer) {
                    dlog("[HTTP] Failed to allocate fallback body buffer\n");
                    return fail_session(session, pcb, p, ERR_MEM);
                }
                memcpy(session->body_buffer, header_end, body_in_header);
                session->total_received = body_in_header;
                session->body_buffer[session->total_received] = '\0';
                session->body_buffer_size = body_in_header + 1;
                dtrace("[HTTP] Fallback: captured %d body bytes in header packet\n",
                       (int)body_in_header);
            }
        }
    } else if (content_length == 0) {
        session->fallback_mode = false;
        session->expected_length = 0;
        session->total_received = 0;
        session->transfer_complete = true;
        session->state = HTTP_SESSION_COMPLETE;
        if (session->completion_callback)
            session->completion_callback("", 0, true, session->callback_arg);
        altcp_recved(pcb, p->tot_len);
        pbuf_free(p);
        altcp_close(pcb);
        session->pcb = NULL;
        reset_session(session);
        return true;
    } else {
        session->fallback_mode = false;
        size_t clen = (size_t)content_length;
        if (clen > SIZE_MAX - 1) {
            dlog("[HTTP] Content-Length too large for platform\n");
            return fail_session(session, pcb, p, ERR_MEM);
        }
        session->expected_length = clen;
        dtrace("[HTTP] Content-Length: %u bytes\n", (unsigned)clen);
        session->total_received = 0;

        // body_in_header: bytes already in the header packet past the CRLFCRLF boundary
        size_t body_in_header =
            session->header_length - (size_t)(header_end - session->header_buffer);
        if (body_in_header > session->expected_length)
            body_in_header = session->expected_length;

        if (!session->no_store_body) {
            session->body_buffer = (char *)malloc(clen + 1);
            if (!session->body_buffer) {
                dlog("[HTTP] Failed to allocate %u bytes for body\n", (unsigned)clen);
                return fail_session(session, pcb, p, ERR_MEM);
            }
            session->body_buffer_size = clen + 1;
            if (body_in_header > 0) {
                memcpy(session->body_buffer, header_end, body_in_header);
                session->total_received = body_in_header;
                dtrace("[HTTP] Found %d body bytes in header packet\n", (int)body_in_header);
            }
        } else {
            if (body_in_header > 0 && session->stream_on_data)
                session->stream_on_data((const uint8_t *)header_end, body_in_header,
                                        session->stream_arg);
            session->total_received = body_in_header;
        }
    }
    return false;
}

// Returns true if caller should return ERR_OK immediately (pcb/pbuf already handled inside).
static bool recv_body_phase(http_session_t *session, struct altcp_pcb *pcb, struct pbuf *p,
                            const char *chunk, int step) {
    if (session->fallback_mode) {
        size_t sstep = (size_t)step;
        if (session->no_store_body) {
            if (session->stream_on_data)
                session->stream_on_data((const uint8_t *)chunk, sstep, session->stream_arg);
            session->total_received += sstep;
        } else {
            if (session->total_received > SIZE_MAX - (sstep + 1)) {
                dlog("[HTTP] Body size overflow in fallback mode\n");
                return fail_session(session, pcb, p, ERR_MEM);
            }
            size_t needed = session->total_received + sstep + 1;
            if (needed > session->body_buffer_size) {
                char *nb = (char *)realloc(session->body_buffer, needed);
                if (!nb) {
                    dlog("[HTTP] realloc failed in fallback mode\n");
                    return fail_session(session, pcb, p, ERR_MEM);
                }
                session->body_buffer = nb;
                session->body_buffer_size = needed;
            }
            memcpy(session->body_buffer + session->total_received, chunk, sstep);
            session->total_received += sstep;
            session->body_buffer[session->total_received] = '\0';
        }
    } else {
        size_t remaining = session->expected_length - session->total_received;
        size_t to_copy = ((size_t)step < remaining) ? (size_t)step : remaining;
        if (to_copy > 0) {
            if (session->no_store_body) {
                if (session->stream_on_data)
                    session->stream_on_data((const uint8_t *)chunk, to_copy, session->stream_arg);
                session->total_received += to_copy;
            } else {
                memcpy(session->body_buffer + session->total_received, chunk, to_copy);
                session->total_received += to_copy;
            }
        }

        int percent = (session->expected_length > 0)
                          ? (int)((session->total_received * 100U) / session->expected_length)
                          : 100;
        if (percent >= s_body_progress_pct + 10) {
            dtrace("[HTTP] Progress: %d%% (%d/%d bytes)\n", percent, (int)session->total_received,
                   (int)session->expected_length);
            s_body_progress_pct = percent;
        }
    }
    return false;
}

static void finalize_transfer(http_session_t *session, struct altcp_pcb *pcb) {
    if (!session->no_store_body && session->body_buffer)
        session->body_buffer[session->expected_length] = '\0';
    session->transfer_complete = true;
    session->state = HTTP_SESSION_COMPLETE;
    dlog("HTTP: %d bytes received\n", (int)session->expected_length);
    session->transfer_was_successful = true;

    if (session->completion_callback) {
        session->completion_callback(session->no_store_body ? NULL : session->body_buffer,
                                     session->expected_length, true, session->callback_arg);
    } else if (session->stream_mode && session->stream_on_complete) {
        session->stream_on_complete(true, session->stream_arg);
    }

    // Deregister callbacks before close so a server-side RST during FIN_WAIT doesn't
    // fire http_error_callback on a stale PCB while the next session is already active.
    altcp_err(pcb, NULL);
    altcp_recv(pcb, NULL);
    altcp_close(pcb);
    session->pcb = NULL;
    reset_session(session);
}

// Two-phase recv: accumulate header until CRLFCRLF, then receive body.
// Fallback handles responses without Content-Length — server close marks end of body.
static err_t http_recv_callback(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    http_session_t *session = (http_session_t *)arg;
    if (pcb != session->pcb) {
        if (p) {
            altcp_recved(pcb, p->tot_len);
            pbuf_free(p);
        }
        altcp_close(pcb);
        dtrace("[HTTP] Ignoring recv from stale PCB\n");
        return ERR_OK;
    }

    if (p)
        dtrace("[HTTP] Received %d bytes from server\n", (int)p->tot_len);

    if (!p) {
        handle_connection_closed(session, pcb);
        return ERR_OK;
    }

    char chunk[HTTP_RECV_SLICE];
    size_t offset = 0;
    while (offset < p->tot_len) {
        int step = pbuf_copy_partial(p, chunk, sizeof(chunk), offset);
        if (step <= 0)
            break;
        offset += step;

        if (!session->header_complete) {
            if (recv_header_phase(session, pcb, p, chunk, step))
                return ERR_OK;
            continue;
        }
        if (session->active && session->header_complete) {
            if (recv_body_phase(session, pcb, p, chunk, step))
                return ERR_OK;
        }
    }

    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    if (session->header_complete && !session->fallback_mode && session->expected_length > 0 &&
        session->total_received >= session->expected_length) {
        finalize_transfer(session, pcb);
    }
    return ERR_OK;
}

static err_t http_connected_callback(void *arg, struct altcp_pcb *pcb, err_t err) {
    http_session_t *session = (http_session_t *)arg;
    if (pcb != session->pcb) {
        dtrace("[HTTP] Connected callback for stale PCB — ignored\n");
        return ERR_OK;
    }

    if (err != ERR_OK) {
        dlog("[HTTP] Connection failed: %d\n", err);
        session->state = HTTP_SESSION_ERROR;
        session->last_error = err;
        if (session->completion_callback)
            session->completion_callback(NULL, 0, false, session->callback_arg);
        else if (session->stream_mode && session->stream_on_complete)
            session->stream_on_complete(false, session->stream_arg);
        reset_session(session);
        return err; // lwIP fires http_error_callback; session inactive so it exits immediately
    }

    // Success: TCP connection established
    dlog("[HTTP] TCP connection established successfully\n");
    if (session->use_tls) {
        dlog("[HTTP] TLS handshake will begin...\n");
    }

    dtrace("[HTTP] Connected to server\n");
    session->state = HTTP_SESSION_CONNECTED;

    // Send HTTP request — log first line (method + path) without the full token body
    {
        char req_line[80] = {0};
        const char *nl = strchr(session->request_data, '\n');
        size_t rl = nl ? (size_t)(nl - session->request_data) : 79;
        if (rl > 79)
            rl = 79;
        memcpy(req_line, session->request_data, rl);
        dlog("[HTTP] Sending %d bytes: %s\n", (int)session->request_length, req_line);
    }
    // Wrap write in cyw43 lock for safety
    cyw43_arch_lwip_begin();
    err_t write_err =
        altcp_write(pcb, session->request_data, session->request_length, TCP_WRITE_FLAG_COPY);
    if (write_err == ERR_OK)
        altcp_output(pcb);
    cyw43_arch_lwip_end();
    if (write_err != ERR_OK) {
        dlog("[HTTP] Failed to send request: %d\n", write_err);
        session->state = HTTP_SESSION_ERROR;
        session->last_error = write_err;
        // Close and reset to avoid dangling PCB or leaks
        altcp_close(pcb);
        session->pcb = NULL;
        if (session->completion_callback) {
            session->completion_callback(NULL, 0, false, session->callback_arg);
        }
        reset_session(session);
        return write_err;
    }

    session->state = HTTP_SESSION_SENDING;
    dtrace("[HTTP] Request sent (%d bytes)\n", (int)session->request_length);

    return ERR_OK;
}

// ERR_RST/ERR_ABRT after a successful fallback-mode transfer is a normal HTTP/1.0 server close,
// not a real error. Errors arriving after a successful transfer are late duplicates.
static void http_error_callback(void *arg, err_t err) {
    http_session_t *session = (http_session_t *)arg;
    // If session is already inactive (e.g., after success/reset), ignore spurious errors
    if (!session || !session->active) {
        dtrace("[HTTP] Ignoring TCP error on inactive session (%d)\n", err);
        return;
    }

    // Ignore errors after a successful transfer (late/duplicate callbacks)
    if (session->transfer_was_successful) {
        dtrace("[HTTP] TCP connection closed normally after successful transfer\n");
        return;
    }

    // Log all errors for debugging, even close notifications
    dlog("[HTTP] TCP error occurred: %d\n", err);

    // Silently ignore pure close notifications for callback logic
    if (err == ERR_CLSD) {
        return;
    }

    // Handle HTTP/1.0 connection close as success if we received data
    if (session->fallback_mode && session->header_complete && session->total_received > 0 &&
        (err == ERR_RST || err == ERR_ABRT)) {

        dlog("[HTTP] HTTP/1.0 connection closed normally after receiving %d bytes\n",
             (int)session->total_received);

        if (!session->no_store_body && session->body_buffer) {
            session->body_buffer[session->total_received] = '\0';
        }
        session->transfer_was_successful = true;

        if (session->completion_callback) {
            session->completion_callback(session->no_store_body ? NULL : session->body_buffer,
                                         session->total_received, true, session->callback_arg);
        }
        reset_session(session);
        return;
    }

    // Genuine error
    dlog("[HTTP] TCP error: %d (transfer incomplete)\n", err);
    session->state = HTTP_SESSION_ERROR;
    session->last_error = err;
    if (session->completion_callback) {
        session->completion_callback(NULL, 0, false, session->callback_arg);
    }
    reset_session(session);
}

// === Public API Implementation ===

bool http_client_init(void) {
    tls_trust_store_init();
    reset_session(&g_session);
    debug_status("OK", "HTTP client initialized\n");
    return true;
}

// Allocates the request buffer, creates a PCB (TLS or plain), registers lwIP callbacks,
// and initiates the TCP connection.  Assumes g_session fields (callbacks, options) are
// already set by the caller.  On error the session is fully reset.
static http_result_t open_connection(const ip_addr_t *server_ip, uint16_t port,
                                     const char *request_data, bool no_store, bool use_tls) {
    s_body_progress_pct = -10;
    g_server_time_valid = false;
    g_session.no_store_body = no_store;
    g_session.request_length = strlen(request_data);
    g_session.request_data = malloc(g_session.request_length + 1);
    if (!g_session.request_data) {
        dlog("[HTTP] Failed to allocate request buffer\n");
        return HTTP_ERROR_MEMORY;
    }
    strcpy(g_session.request_data, request_data);

    cyw43_arch_lwip_begin();

    g_session.use_tls = use_tls;
    if (g_session.use_tls) {
        extract_host_from_request(g_session.request_data, g_session.server_hostname,
                                  sizeof(g_session.server_hostname));
    }

    if (g_session.use_tls) {
        struct altcp_tls_config *cfg = tls_get_client_config();
        if (!cfg) {
            cyw43_arch_lwip_end();
            dlog("[TLS] No client config (CA) available; cannot create TLS PCB\n");
            free(g_session.request_data);
            g_session.request_data = NULL;
            return HTTP_ERROR_CONNECTION;
        }
        u8_t ip_type = IPADDR_TYPE_V4;
#if LWIP_IPV6
        ip_type = IP_IS_V6(server_ip) ? IPADDR_TYPE_V6 : IPADDR_TYPE_V4;
#endif
        g_session.pcb = altcp_tls_new(cfg, ip_type);
        if (g_session.pcb && g_session.server_hostname[0]) {
            void *ctx = altcp_tls_context(g_session.pcb);
            if (ctx) {
                mbedtls_ssl_set_hostname((mbedtls_ssl_context *)ctx, g_session.server_hostname);
            }
        }
        dlog("[HTTP] Using TLS: host='%s' port=%u\n",
             g_session.server_hostname[0] ? g_session.server_hostname : "(none)", (unsigned)port);
    } else {
        g_session.pcb = altcp_new(NULL);
    }
    if (!g_session.pcb) {
        cyw43_arch_lwip_end();
        dlog("[HTTP] Failed to create PCB\n");
        free(g_session.request_data);
        g_session.request_data = NULL;
        return HTTP_ERROR_CONNECTION;
    }

    altcp_arg(g_session.pcb, &g_session);
    altcp_recv(g_session.pcb, http_recv_callback);
    altcp_err(g_session.pcb, http_error_callback);

    g_session.active = true;
    g_session.state = HTTP_SESSION_CONNECTING;

    dlog("[HTTP] Calling altcp_connect to %d.%d.%d.%d:%u\n", (int)((server_ip->addr >> 0) & 0xFF),
         (int)((server_ip->addr >> 8) & 0xFF), (int)((server_ip->addr >> 16) & 0xFF),
         (int)((server_ip->addr >> 24) & 0xFF), (unsigned)port);
    err_t err = altcp_connect(g_session.pcb, server_ip, port, http_connected_callback);
    cyw43_arch_lwip_end();

    dlog("[HTTP] altcp_connect returned: %d\n", err);
    if (err != ERR_OK) {
        dlog("[HTTP] Failed to connect: %d\n", err);
        cyw43_arch_lwip_begin();
        altcp_close(g_session.pcb);
        cyw43_arch_lwip_end();
        g_session.pcb = NULL;
        reset_session(&g_session);
        return HTTP_ERROR_CONNECTION;
    }

    return HTTP_SUCCESS;
}

http_result_t
http_request_async(const ip_addr_t *server_ip, uint16_t port, const char *request_data,
                   void (*callback)(const char *body, size_t length, bool success, void *arg),
                   void *callback_arg) {
    reset_session(&g_session);
    g_session.completion_callback = callback;
    g_session.callback_arg = callback_arg;
    return open_connection(server_ip, port, request_data, false, port == 443);
}

http_result_t http_request_async_count_only(const ip_addr_t *server_ip, uint16_t port,
                                            const char *request_data,
                                            void (*callback)(const char *body, size_t length,
                                                             bool success, void *arg),
                                            void *callback_arg) {
    reset_session(&g_session);
    g_session.completion_callback = callback;
    g_session.callback_arg = callback_arg;
    return open_connection(server_ip, port, request_data, true, port == 443);
}

// --- Synchronous request helpers ---

static volatile bool s_sync_done = false;
static volatile bool s_sync_ok = false;

// Data callback: set per-request by http_request_sync; set explicitly by
// Homematic's async orchestration via set_data_callback/http_invoke_data_callback.
static data_callback_fn data_callback = NULL;
static void *data_callback_arg = NULL;

static void http_sync_wrapper_cb(const char *body, size_t length, bool success, void *arg) {
    (void)arg;
    if (success && body && length > 0)
        http_invoke_data_callback(body, length, NULL);
    s_sync_ok = success;
    s_sync_done = true;
}

static bool http_request_sync_impl(const ip_addr_t *server_ip, uint16_t port, const char *request,
                                   int timeout_ms, bool no_store, data_callback_fn cb,
                                   void *cb_arg) {
    if (timeout_ms <= 0)
        timeout_ms = 5000;
    data_callback = cb;
    data_callback_arg = cb_arg;
    s_sync_done = false;
    s_sync_ok = false;
    reset_session(&g_session);
    g_session.completion_callback = http_sync_wrapper_cb;
    if (open_connection(server_ip, port, request, no_store, port == 443) != HTTP_SUCCESS)
        return false;
    int waited = 0;
    while (!s_sync_done && waited < timeout_ms) {
        sleep_ms(50);
        watchdog_update();
        waited += 50;
    }
    if (!s_sync_done)
        dlog("[HTTP] http_request_sync timeout after %d ms\n", waited);
    return s_sync_done && s_sync_ok;
}

bool http_request_sync(const ip_addr_t *server_ip, uint16_t port, const char *request,
                       int timeout_ms, data_callback_fn cb, void *cb_arg) {
    return http_request_sync_impl(server_ip, port, request, timeout_ms, false, cb, cb_arg);
}

bool http_request_sync_count_only(const ip_addr_t *server_ip, uint16_t port, const char *request,
                                  int timeout_ms) {
    return http_request_sync_impl(server_ip, port, request, timeout_ms, true, NULL, NULL);
}

http_result_t
http_request_async_stream(const ip_addr_t *server_ip, uint16_t port, const char *request_data,
                          void (*on_header)(const char *header, size_t header_len, int status_code,
                                            int content_length, void *arg),
                          void (*on_data)(const uint8_t *data, size_t len, void *arg),
                          void (*on_complete)(bool success, void *arg), void *cb_arg) {
    reset_session(&g_session);
    g_session.stream_mode = true;
    g_session.stream_on_header = on_header;
    g_session.stream_on_data = on_data;
    g_session.stream_on_complete = on_complete;
    g_session.stream_arg = cb_arg;
    return open_connection(server_ip, port, request_data, true, port == 443);
}

bool http_session_is_active(void) { return g_session.active; }

// =============================================================================
// CALLBACK SYSTEM AND RUN HELPERS
// =============================================================================

void set_data_callback(data_callback_fn callback, void *arg) {
    data_callback = callback;
    data_callback_arg = arg;
}

void http_invoke_data_callback(const char *data, size_t length, void *arg) {
    if (data_callback)
        data_callback(data, length, arg);
}

bool http_get_server_time(rtc_time_t *out) {
    if (!g_server_time_valid || !out) {
        return false;
    }
    *out = g_server_time;
    return true;
}

int http_get_last_status_code(void) { return g_last_status_code; }
