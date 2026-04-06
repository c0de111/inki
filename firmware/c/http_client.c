#include "http_client.h"
#include "boot_input.h"
#define LOG_MODULE LOG_MOD_HTTP
#include "debug.h"
#include "flash.h"
#include "inki_monitor.h"
#include "sensors.h"
#include "wifi.h"

#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "lwip/altcp_tls.h"
#include "lwip/netif.h"
#include "mbedtls/ssl.h"
#include "tls_trust_store.h"

#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Global session (single session for now, can be extended later)
static http_session_t g_session = {0};
static bool g_transfer_was_successful = false;
static bool g_next_no_store = false;

// Synchronous operation tracking
static bool sync_operation_complete = false;
static bool sync_operation_success = false;

// Optional hook to notify after a TCP connection is fully closed
static void (*g_after_close_cb)(void *) = NULL;
static void *g_after_close_arg = NULL;

void http_sync_reset(void) {
    sync_operation_complete = false;
    sync_operation_success = false;
}

bool http_sync_is_complete(void) { return sync_operation_complete; }
bool http_sync_is_success(void) { return sync_operation_success; }

void http_sync_signal(bool success) {
    sync_operation_success = success;
    sync_operation_complete = true;
}

// Server time extracted from HTTP Date header (UTC)
static rtc_time_t g_server_time = {0};
static bool g_server_time_valid = false;

void http_set_after_close(void (*cb)(void *), void *arg) {
    g_after_close_cb = cb;
    g_after_close_arg = arg;
}

// === Helper Functions ===

/**
 * @brief Reset HTTP session to initial state
 * @param session Session structure to reset
 *
 * Cleans up all allocated memory, resets state variables, and prepares
 * the session for reuse. Safe to call multiple times.
 */

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

/**
 * @brief Parse Content-Length from HTTP header
 * @param header HTTP header string to parse
 * @return Content-Length value or -1 if not found
 *
 * Searches for Content-Length header (case-insensitive) and extracts
 * the numeric value. Returns -1 if header is missing or invalid.
 */
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

// Parse HTTP Date header (RFC 7231) into rtc_time_t (UTC)
static bool parse_http_date_header(const char *header, rtc_time_t *out) {
    const char *d = strstr(header, "Date:");
    if (!d) {
        d = strstr(header, "date:");
    }
    if (!d) {
        return false;
    }

    d += 5;
    while (*d == ' ')
        d++;

    // Skip day name and comma: "Fri, "
    const char *comma = strchr(d, ',');
    if (!comma) {
        return false;
    }
    d = comma + 1;
    while (*d == ' ')
        d++;

    int day, year, hour, minute, second;
    char mon[4] = {0};
    if (sscanf(d, "%d %3s %d %d:%d:%d", &day, mon, &year, &hour, &minute, &second) != 6) {
        return false;
    }

    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int month = 0;
    for (int i = 0; i < 12; i++) {
        if (strncmp(mon, months[i], 3) == 0) {
            month = i + 1;
            break;
        }
    }
    if (month == 0) {
        return false;
    }

    // Compute weekday (ISO 8601: 1=Mon..7=Sun) using Tomohiko Sakamoto's algorithm
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = year;
    if (month < 3)
        y--;
    int dow = (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;
    // dow: 0=Sun,1=Mon..6=Sat -> convert to 1=Mon..7=Sun
    int weekday = (dow == 0) ? 7 : dow;

    *out = (rtc_time_t){.seconds = (uint8_t)second,
                        .minutes = (uint8_t)minute,
                        .hours = (uint8_t)hour,
                        .day = (uint8_t)weekday,
                        .date = (uint8_t)day,
                        .month = (uint8_t)month,
                        .year = (uint8_t)(year - 2000)};
    return true;
}

/**
 * @brief Parse HTTP status code from the status line
 * @param header HTTP header string
 * @return status code (e.g., 200) or -1 if not found
 */
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
    const char *p = request;
    // Ensure we also scan the first line
    if (strncasecmp(p, "Host:", 5) == 0) {
        const char *v = p + 5;
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
    while ((p = strstr(p, "\n")) != NULL) {
        p++;
        if (strncasecmp(p, "Host:", 5) == 0) {
            const char *v = p + 5;
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
    }
}

// === TCP Callbacks (based on historian architecture) ===

/**
 * @brief TCP receive callback for HTTP data processing
 * @param arg User argument (http_session_t pointer)
 * @param pcb TCP protocol control block
 * @param p Received packet buffer (NULL if connection closed)
 * @param err Error status
 * @return ERR_OK on success
 *
 * This callback handles incoming TCP data in two phases:
 * 1. Header collection: Accumulates HTTP headers until "\r\n\r\n" is found
 * 2. Body reception: Processes response body based on Content-Length or fallback mode
 *
 * Features fallback mode for responses without Content-Length (SeatSurfing compatibility).
 */
static err_t http_recv_callback(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    http_session_t *session = (http_session_t *)arg;
    // Ignore stray callbacks from stale PCBs (previous session) to avoid corrupting current
    // transfer
    if (pcb != session->pcb) {
        if (p) {
            altcp_recved(pcb, p->tot_len);
            pbuf_free(p);
        }
        // Ensure the stale pcb is closed
        altcp_close(pcb);
        dtrace("[HTTP] Ignoring recv from stale PCB\n");
        return ERR_OK;
    }

    if (p) {
        dtrace("[HTTP] Received %d bytes from server\n", (int)p->tot_len);
    }

    if (!p) {
        // Connection closed
        debug_log("[HTTP] Connection closed by server\n");
        altcp_close(pcb);
        session->pcb = NULL;

        // Handle fallback mode (no Content-Length): connection close marks completion
        if (session->active && !session->transfer_complete && session->header_complete &&
            session->fallback_mode) {
            session->transfer_complete = (session->total_received > 0);
            if (session->transfer_complete) {
                if (!session->no_store_body && session->body_buffer) {
                    session->body_buffer[session->total_received] = '\0';
                }
                g_transfer_was_successful = true;
                if (session->completion_callback) {
                    session->completion_callback(
                        session->no_store_body ? NULL : session->body_buffer,
                        session->total_received, true, session->callback_arg);
                } else if (session->no_store_body && session->stream_on_complete) {
                    session->stream_on_complete(true, session->stream_arg);
                }
                reset_session(session);
                return ERR_OK;
            }
        }

        // Only call callback if transfer was NOT complete (error case)
        if (session->active && !session->transfer_complete) {
            if (session->completion_callback) {
                session->completion_callback(NULL, 0, false, session->callback_arg);
            } else if (session->no_store_body && session->stream_on_complete) {
                session->stream_on_complete(false, session->stream_arg);
            }
        }
        reset_session(session);
        // Notify close hook for sequencing (e.g., Homematic next request)
        if (g_after_close_cb) {
            void (*cb)(void *) = g_after_close_cb;
            void *cb_arg = g_after_close_arg;
            // Clear before calling to avoid reentry
            g_after_close_cb = NULL;
            g_after_close_arg = NULL;
            cb(cb_arg);
        }
        return ERR_OK;
    }

    // Buffer for streaming slices
    char chunk[HTTP_RECV_SLICE];
    size_t offset = 0;

    while (offset < p->tot_len) {
        int step = pbuf_copy_partial(p, chunk, sizeof(chunk), offset);
        if (step <= 0)
            break;
        offset += step;

        // === Phase 1: Header collection ===
        if (!session->header_complete) {
            size_t space = sizeof(session->header_buffer) - session->header_length - 1;
            size_t to_copy = (step < (int)space) ? (size_t)step : space;

            if (to_copy > 0) {
                memcpy(session->header_buffer + session->header_length, chunk, to_copy);
                session->header_length += to_copy;
                session->header_buffer[session->header_length] = '\0';
            }

            dtrace("[HTTP] Header chunk: %d bytes (total: %d)\n", step,
                   (int)session->header_length);

            // If header buffer exhausted without CRLFCRLF, fail
            if (to_copy < (size_t)step && !strstr(session->header_buffer, "\r\n\r\n")) {
                debug_log_with_color(COLOR_RED, "[HTTP] Header too large for buffer\n");
                altcp_recved(pcb, p->tot_len);
                pbuf_free(p);
                altcp_close(pcb);
                session->state = HTTP_SESSION_ERROR;
                session->last_error = ERR_BUF;
                if (session->completion_callback) {
                    session->completion_callback(NULL, 0, false, session->callback_arg);
                }
                reset_session(session);
                return ERR_OK;
            }

            // Check for header end
            char *header_end = strstr(session->header_buffer, "\r\n\r\n");
            if (header_end) {
                session->header_complete = true;
                session->state = HTTP_SESSION_RECEIVING_BODY;
                header_end += 4; // Skip past CRLFCRLF

                // Extract server time from Date header (UTC)
                g_server_time_valid =
                    parse_http_date_header(session->header_buffer, &g_server_time);

                // Parse and validate HTTP status
                int status = parse_http_status_code(session->header_buffer);
                if (status >= 0 && (status < 200 || status >= 300)) {
                    debug_log_with_color(COLOR_RED, "[HTTP] Error status: %d\n", status);
                    altcp_recved(pcb, p->tot_len);
                    pbuf_free(p);
                    altcp_close(pcb);
                    session->state = HTTP_SESSION_ERROR;
                    session->last_error = ERR_CLSD;
                    if (session->completion_callback) {
                        session->completion_callback(NULL, 0, false, session->callback_arg);
                    }
                    reset_session(session);
                    return ERR_OK;
                }

                // Reject unsupported Transfer-Encoding: chunked
                const char *te1 = strstr(session->header_buffer, "Transfer-Encoding:");
                const char *te2 = te1 ? te1 : strstr(session->header_buffer, "transfer-encoding:");
                if (te2 && (strstr(te2, "chunked") || strstr(te2, "Chunked"))) {
                    debug_log_with_color(COLOR_RED,
                                         "[HTTP] Chunked transfer-encoding not supported\n");
                    altcp_recved(pcb, p->tot_len);
                    pbuf_free(p);
                    altcp_close(pcb);
                    session->state = HTTP_SESSION_ERROR;
                    session->last_error = ERR_VAL;
                    if (session->completion_callback) {
                        session->completion_callback(NULL, 0, false, session->callback_arg);
                    }
                    reset_session(session);
                    return ERR_OK;
                }

                // Parse Content-Length
                int content_length = parse_content_length(session->header_buffer);
                // Inform streaming client about header if requested
                if ((session->no_store_body || session->stream_mode) && session->stream_on_header) {
                    int status_code = parse_http_status_code(session->header_buffer);
                    session->stream_on_header(session->header_buffer, session->header_length,
                                              status_code, content_length, session->stream_arg);
                }
                if (content_length < 0) {
                    // No Content-Length: start fallback dynamic accumulation
                    session->fallback_mode = true;
                    session->expected_length = 0; // not used in fallback mode
                    session->body_buffer = NULL;
                    session->body_buffer_size = 0;
                    session->total_received = 0;

                    // Copy/stream any body bytes already captured in header buffer
                    size_t body_in_header =
                        session->header_length - (header_end - session->header_buffer);
                    if (body_in_header > 0) {
                        if (session->no_store_body && session->stream_on_data) {
                            session->stream_on_data((const uint8_t *)header_end, body_in_header,
                                                    session->stream_arg);
                            session->total_received = body_in_header;
                        } else {
                            session->body_buffer = (char *)malloc(body_in_header + 1);
                            if (!session->body_buffer) {
                                debug_log_with_color(
                                    COLOR_RED, "[HTTP] Failed to allocate fallback body buffer\n");
                                altcp_recved(pcb, p->tot_len);
                                pbuf_free(p);
                                altcp_close(pcb);
                                session->state = HTTP_SESSION_ERROR;
                                session->last_error = ERR_MEM;
                                if (session->completion_callback) {
                                    session->completion_callback(NULL, 0, false,
                                                                 session->callback_arg);
                                }
                                reset_session(session);
                                return ERR_OK;
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
                    // Explicit zero-length body; complete immediately
                    session->fallback_mode = false;
                    session->expected_length = 0;
                    session->total_received = 0;
                    session->transfer_complete = true;
                    session->state = HTTP_SESSION_COMPLETE;
                    if (session->completion_callback) {
                        session->completion_callback("", 0, true, session->callback_arg);
                    }
                    altcp_recved(pcb, p->tot_len);
                    pbuf_free(p);
                    altcp_close(pcb);
                    session->pcb = NULL;
                    reset_session(session);
                    return ERR_OK;
                } else {
                    session->fallback_mode = false;
                    size_t clen = (size_t)content_length;
                    // Guard against overflow in allocation (clen + 1)
                    if (clen > SIZE_MAX - 1) {
                        debug_log_with_color(COLOR_RED,
                                             "[HTTP] Content-Length too large for platform\n");
                        altcp_recved(pcb, p->tot_len);
                        pbuf_free(p);
                        altcp_close(pcb);
                        session->state = HTTP_SESSION_ERROR;
                        session->last_error = ERR_MEM;
                        if (session->completion_callback) {
                            session->completion_callback(NULL, 0, false, session->callback_arg);
                        }
                        reset_session(session);
                        return ERR_OK;
                    }
                    session->expected_length = clen;
                    dtrace("[HTTP] Content-Length: %u bytes\n", (unsigned)clen);

                    session->total_received = 0;

                    if (!session->no_store_body) {
                        // Allocate body buffer
                        session->body_buffer = (char *)malloc(clen + 1);
                        if (!session->body_buffer) {
                            debug_log_with_color(COLOR_RED,
                                                 "[HTTP] Failed to allocate %u bytes for body\n",
                                                 (unsigned)clen);
                            altcp_recved(pcb, p->tot_len);
                            pbuf_free(p);
                            altcp_close(pcb);
                            session->state = HTTP_SESSION_ERROR;
                            session->last_error = ERR_MEM;
                            if (session->completion_callback) {
                                session->completion_callback(NULL, 0, false, session->callback_arg);
                            }
                            reset_session(session);
                            return ERR_OK;
                        }
                        session->body_buffer_size = clen + 1;

                        // Copy any body data that was already captured in header buffer
                        size_t body_in_header =
                            session->header_length - (header_end - session->header_buffer);
                        if (body_in_header > 0) {
                            if (body_in_header > session->expected_length) {
                                body_in_header = session->expected_length;
                            }
                            memcpy(session->body_buffer, header_end, body_in_header);
                            session->total_received = body_in_header;
                            dtrace("[HTTP] Found %d body bytes in header packet\n",
                                   (int)body_in_header);
                        }
                    } else {
                        // Count/stream: account for any body already in this packet
                        size_t body_in_header =
                            session->header_length - (header_end - session->header_buffer);
                        if (body_in_header > session->expected_length) {
                            body_in_header = session->expected_length;
                        }
                        if (body_in_header > 0 && session->stream_on_data) {
                            session->stream_on_data((const uint8_t *)header_end, body_in_header,
                                                    session->stream_arg);
                        }
                        session->total_received = body_in_header;
                    }
                }
            }

            // Continue loop to process remaining slices (if any)
            continue;
        }

        // === Phase 2: Body reception ===
        if (session->active && session->header_complete) {
            if (session->fallback_mode) {
                // Fallback mode: dynamically grow body buffer
                size_t sstep = (size_t)step;
                if (session->no_store_body) {
                    // Stream/count only in fallback mode
                    if (session->stream_on_data) {
                        session->stream_on_data((const uint8_t *)chunk, sstep, session->stream_arg);
                    }
                    session->total_received += sstep;
                } else {
                    // Overflow guard for needed calculation
                    if (session->total_received > SIZE_MAX - (sstep + 1)) {
                        debug_log_with_color(COLOR_RED,
                                             "[HTTP] Body size overflow in fallback mode\n");
                        altcp_recved(pcb, p->tot_len);
                        pbuf_free(p);
                        altcp_close(pcb);
                        session->state = HTTP_SESSION_ERROR;
                        session->last_error = ERR_MEM;
                        if (session->completion_callback) {
                            session->completion_callback(NULL, 0, false, session->callback_arg);
                        }
                        reset_session(session);
                        return ERR_OK;
                    }
                    size_t needed = session->total_received + sstep + 1;
                    if (needed > session->body_buffer_size) {
                        size_t new_cap = needed;
                        char *nb = (char *)realloc(session->body_buffer, new_cap);
                        if (!nb) {
                            debug_log_with_color(COLOR_RED,
                                                 "[HTTP] realloc failed in fallback mode\n");
                            altcp_recved(pcb, p->tot_len);
                            pbuf_free(p);
                            altcp_close(pcb);
                            session->state = HTTP_SESSION_ERROR;
                            session->last_error = ERR_MEM;
                            if (session->completion_callback) {
                                session->completion_callback(NULL, 0, false, session->callback_arg);
                            }
                            reset_session(session);
                            return ERR_OK;
                        }
                        session->body_buffer = nb;
                        session->body_buffer_size = new_cap;
                    }
                    memcpy(session->body_buffer + session->total_received, chunk, sstep);
                    session->total_received += sstep;
                    session->body_buffer[session->total_received] = '\0';
                }
            } else {
                // Normal mode with Content-Length
                size_t remaining = session->expected_length - session->total_received;
                size_t to_copy = ((size_t)step < remaining) ? (size_t)step : remaining;
                if (to_copy > 0) {
                    if (session->no_store_body) {
                        if (session->stream_on_data) {
                            session->stream_on_data((const uint8_t *)chunk, to_copy,
                                                    session->stream_arg);
                        }
                        session->total_received += to_copy;
                    } else {
                        memcpy(session->body_buffer + session->total_received, chunk, to_copy);
                        session->total_received += to_copy;
                    }
                }

                // Progress logging every 10%
                static int last_percent = -10;
                int percent =
                    (session->expected_length > 0)
                        ? (int)((session->total_received * 100U) / session->expected_length)
                        : 100;
                if (percent >= last_percent + 10) {
                    dtrace("[HTTP] Progress: %d%% (%d/%d bytes)\n", percent,
                           (int)session->total_received, (int)session->expected_length);
                    last_percent = percent;
                }
            }
        }
    }

    // Acknowledge and free pbuf chain
    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    // Check for completion in Content-Length mode
    if (session->header_complete && !session->fallback_mode && session->expected_length > 0 &&
        session->total_received >= session->expected_length) {
        goto transfer_complete;
    }

    return ERR_OK;

transfer_complete:
    if (!session->no_store_body && session->body_buffer) {
        session->body_buffer[session->expected_length] = '\0';
    }
    session->transfer_complete = true;
    session->state = HTTP_SESSION_COMPLETE;

    debug_status("OK", "HTTP: %d bytes received\n", (int)session->expected_length);

    // Set global flags
    g_transfer_was_successful = true;

    // Call callback with complete data
    if (session->completion_callback) {
        session->completion_callback(session->no_store_body ? NULL : session->body_buffer,
                                     session->expected_length, true, session->callback_arg);
    } else if (session->no_store_body && session->stream_on_complete) {
        session->stream_on_complete(true, session->stream_arg);
    }

    // Clean up connection
    altcp_close(pcb);
    session->pcb = NULL;
    // Ensure session is fully reset before any potential next request is started
    reset_session(session);
    // Notify close hook for sequencing (e.g., start next Homematic request)
    if (g_after_close_cb) {
        void (*cb)(void *) = g_after_close_cb;
        void *cb_arg = g_after_close_arg;
        g_after_close_cb = NULL;
        g_after_close_arg = NULL;
        cb(cb_arg);
    }

    return ERR_OK;
}

/**
 * @brief TCP connection established callback
 * @param arg User argument (http_session_t pointer)
 * @param pcb TCP protocol control block
 * @param err Connection error status
 * @return ERR_OK on success
 *
 * Called when TCP connection is established. Sends the HTTP request
 * and transitions session to sending state.
 */
static err_t http_connected_callback(void *arg, struct altcp_pcb *pcb, err_t err) {
    http_session_t *session = (http_session_t *)arg;
    if (pcb != session->pcb) {
        dtrace("[HTTP] Connected callback for stale PCB — ignored\n");
        return ERR_OK;
    }

    if (err != ERR_OK) {
        debug_log_with_color(COLOR_RED, "[HTTP] Connection failed: %d\n", err);
        session->state = HTTP_SESSION_ERROR;
        session->last_error = err;
        return err;
    }

    // Success: TCP connection established
    debug_log("[HTTP] TCP connection established successfully\n");
    if (session->use_tls) {
        debug_log("[HTTP] TLS handshake will begin...\n");
    }

    dtrace("[HTTP] Connected to server\n");
    session->state = HTTP_SESSION_CONNECTED;

    // Send HTTP request
    debug_log("[HTTP] Sending HTTP request (%d bytes)\n", (int)session->request_length);
    // Wrap write in cyw43 lock for safety
    cyw43_arch_lwip_begin();
    err_t write_err =
        altcp_write(pcb, session->request_data, session->request_length, TCP_WRITE_FLAG_COPY);
    if (write_err == ERR_OK)
        altcp_output(pcb);
    cyw43_arch_lwip_end();
    if (write_err != ERR_OK) {
        debug_log_with_color(COLOR_RED, "[HTTP] Failed to send request: %d\n", write_err);
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

/**
 * @brief TCP error callback
 * @param arg User argument (http_session_t pointer)
 * @param err TCP error code
 *
 * Called when TCP error occurs. Distinguishes between normal connection
 * close after successful transfer and actual errors.
 */
static void http_error_callback(void *arg, err_t err) {
    http_session_t *session = (http_session_t *)arg;
    // If session is already inactive (e.g., after success/reset), ignore spurious errors
    if (!session || !session->active) {
        dtrace("[HTTP] Ignoring TCP error on inactive session (%d)\n", err);
        return;
    }

    // Ignore errors after a successful transfer (late/duplicate callbacks)
    if (g_transfer_was_successful) {
        dtrace("[HTTP] TCP connection closed normally after successful transfer\n");
        return;
    }

    // Log all errors for debugging, even close notifications
    debug_log_with_color(COLOR_RED, "[HTTP] TCP error occurred: %d\n", err);

    // Silently ignore pure close notifications for callback logic
    if (err == ERR_CLSD) {
        return;
    }

    // Handle HTTP/1.0 connection close as success if we received data
    if (!g_transfer_was_successful && session->fallback_mode && session->header_complete &&
        session->total_received > 0 && (err == ERR_RST || err == -13)) {

        debug_log("[HTTP] HTTP/1.0 connection closed normally after receiving %d bytes\n",
                  (int)session->total_received);

        if (!session->no_store_body && session->body_buffer) {
            session->body_buffer[session->total_received] = '\0';
        }
        g_transfer_was_successful = true;

        if (session->completion_callback) {
            session->completion_callback(session->no_store_body ? NULL : session->body_buffer,
                                         session->total_received, true, session->callback_arg);
        }
        reset_session(session);
        return;
    }

    // Genuine error before success
    if (!g_transfer_was_successful) {
        debug_log_with_color(COLOR_RED, "[HTTP] TCP error: %d (transfer incomplete)\n", err);

        session->state = HTTP_SESSION_ERROR;
        session->last_error = err;

        if (session->completion_callback) {
            session->completion_callback(NULL, 0, false, session->callback_arg);
        }
        // Ensure all buffers and state are cleaned up on error
        reset_session(session);
    }

    session->pcb = NULL;
}

// === Public API Implementation ===

bool http_client_init(void) {
    tls_trust_store_init();
    reset_session(&g_session);
    debug_status("OK", "HTTP client initialized\n");
    return true;
}

http_result_t
http_request_async(const ip_addr_t *server_ip, uint16_t port, const char *request_data,
                   void (*callback)(const char *body, size_t length, bool success, void *arg),
                   void *callback_arg) {
    // Reset any previous session
    reset_session(&g_session);
    // Apply next option flags (e.g., no-store) then clear
    g_session.no_store_body = g_next_no_store;
    g_next_no_store = false;
    g_transfer_was_successful = false;

    // Store request data
    g_session.request_length = strlen(request_data);
    g_session.request_data = malloc(g_session.request_length + 1);
    if (!g_session.request_data) {
        debug_log_with_color(COLOR_RED, "[HTTP] Failed to allocate request buffer\n");
        return HTTP_ERROR_MEMORY;
    }
    strcpy(g_session.request_data, request_data);

    // Store callback
    g_session.completion_callback = callback;
    g_session.callback_arg = callback_arg;

    dtrace("[HTTP] Creating TCP connection...\n");

    // Perform lwIP operations under cyw43 lock
    cyw43_arch_lwip_begin();

    // Decide on TLS based on port (443 -> TLS) and set SNI from Host header
    g_session.use_tls = (port == 443);
    if (g_session.use_tls) {
        extract_host_from_request(g_session.request_data, g_session.server_hostname,
                                  sizeof(g_session.server_hostname));
    }

    // Create PCB (TLS or plain)
    if (g_session.use_tls) {
        struct altcp_tls_config *cfg = tls_get_client_config();
        if (!cfg) {
            cyw43_arch_lwip_end();
            debug_log_with_color(COLOR_RED,
                                 "[TLS] No client config (CA) available; cannot create TLS PCB\n");
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
            // Set SNI/hostname for certificate verification
            void *ctx = altcp_tls_context(g_session.pcb);
            if (ctx) {
                mbedtls_ssl_set_hostname((mbedtls_ssl_context *)ctx, g_session.server_hostname);
            }
        }
        debug_log("[HTTP] Using TLS: host='%s' port=%u\n",
                  g_session.server_hostname[0] ? g_session.server_hostname : "(none)",
                  (unsigned)port);
    } else {
        g_session.pcb = altcp_new(NULL);
    }
    if (!g_session.pcb) {
        cyw43_arch_lwip_end();
        debug_log_with_color(COLOR_RED, "[HTTP] Failed to create PCB\n");
        free(g_session.request_data);
        g_session.request_data = NULL;
        return HTTP_ERROR_CONNECTION;
    }

    // Set callbacks
    altcp_arg(g_session.pcb, &g_session);
    altcp_recv(g_session.pcb, http_recv_callback);
    altcp_err(g_session.pcb, http_error_callback);

    // Mark session as active
    g_session.active = true;
    g_session.state = HTTP_SESSION_CONNECTING;

    // Connect to server
    debug_log("[HTTP] Calling altcp_connect to %d.%d.%d.%d:%u\n",
              (int)((server_ip->addr >> 0) & 0xFF), (int)((server_ip->addr >> 8) & 0xFF),
              (int)((server_ip->addr >> 16) & 0xFF), (int)((server_ip->addr >> 24) & 0xFF),
              (unsigned)port);
    err_t err = altcp_connect(g_session.pcb, server_ip, port, http_connected_callback);

    cyw43_arch_lwip_end();

    debug_log("[HTTP] altcp_connect returned: %d\n", err);
    if (err != ERR_OK) {
        debug_log_with_color(COLOR_RED, "[HTTP] Failed to connect: %d\n", err);
        // Close PCB under lock
        cyw43_arch_lwip_begin();
        altcp_close(g_session.pcb);
        cyw43_arch_lwip_end();
        g_session.pcb = NULL;
        reset_session(&g_session);
        return HTTP_ERROR_CONNECTION;
    }

    return HTTP_SUCCESS;
}

http_result_t http_request_async_count_only(const ip_addr_t *server_ip, uint16_t port,
                                            const char *request_data,
                                            void (*callback)(const char *body, size_t length,
                                                             bool success, void *arg),
                                            void *callback_arg) {
    g_next_no_store = true;
    return http_request_async(server_ip, port, request_data, callback, callback_arg);
}

http_result_t
http_request_async_stream(const ip_addr_t *server_ip, uint16_t port, const char *request_data,
                          void (*on_header)(const char *header, size_t header_len, int status_code,
                                            int content_length, void *arg),
                          void (*on_data)(const uint8_t *data, size_t len, void *arg),
                          void (*on_complete)(bool success, void *arg), void *cb_arg) {
    // Reset any previous session and configure for streaming (no body accumulation)
    reset_session(&g_session);
    g_session.no_store_body = true;
    g_session.stream_mode = true;
    g_transfer_was_successful = false;

    // Store request data
    g_session.request_length = strlen(request_data);
    g_session.request_data = malloc(g_session.request_length + 1);
    if (!g_session.request_data) {
        debug_log_with_color(COLOR_RED, "[HTTP] Failed to allocate request buffer\n");
        return HTTP_ERROR_MEMORY;
    }
    strcpy(g_session.request_data, request_data);

    // Register streaming callbacks
    g_session.stream_on_header = on_header;
    g_session.stream_on_data = on_data;
    g_session.stream_on_complete = on_complete;
    g_session.stream_arg = cb_arg;

    // Perform lwIP operations under cyw43 lock
    cyw43_arch_lwip_begin();

    // Decide on TLS based on port (443 -> TLS) and set SNI from Host header
    g_session.use_tls = (port == 443);
    if (g_session.use_tls) {
        extract_host_from_request(g_session.request_data, g_session.server_hostname,
                                  sizeof(g_session.server_hostname));
    }

    // Create PCB (TLS or plain)
    if (g_session.use_tls) {
        struct altcp_tls_config *cfg = tls_get_client_config();
        if (!cfg) {
            cyw43_arch_lwip_end();
            debug_log_with_color(COLOR_RED,
                                 "[TLS] No client config (CA) available; cannot create TLS PCB\n");
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
            // Set SNI/hostname for certificate verification
            void *ctx = altcp_tls_context(g_session.pcb);
            if (ctx) {
                mbedtls_ssl_set_hostname((mbedtls_ssl_context *)ctx, g_session.server_hostname);
            }
        }
        debug_log("[HTTP] Using TLS: host='%s' port=%u\n",
                  g_session.server_hostname[0] ? g_session.server_hostname : "(none)",
                  (unsigned)port);
    } else {
        g_session.pcb = altcp_new(NULL);
    }
    if (!g_session.pcb) {
        cyw43_arch_lwip_end();
        debug_log_with_color(COLOR_RED, "[HTTP] Failed to create PCB\n");
        free(g_session.request_data);
        g_session.request_data = NULL;
        return HTTP_ERROR_CONNECTION;
    }

    // Set callbacks
    altcp_arg(g_session.pcb, &g_session);
    altcp_recv(g_session.pcb, http_recv_callback);
    altcp_err(g_session.pcb, http_error_callback);

    // Mark session as active
    g_session.active = true;
    g_session.state = HTTP_SESSION_CONNECTING;

    // Connect to server
    err_t err = altcp_connect(g_session.pcb, server_ip, port, http_connected_callback);
    cyw43_arch_lwip_end();
    if (err != ERR_OK) {
        cyw43_arch_lwip_begin();
        altcp_close(g_session.pcb);
        cyw43_arch_lwip_end();
        g_session.pcb = NULL;
        reset_session(&g_session);
        return HTTP_ERROR_CONNECTION;
    }
    return HTTP_SUCCESS;
}

bool http_session_is_active(void) { return g_session.active; }

// =============================================================================
// CALLBACK SYSTEM AND RUN HELPERS
// =============================================================================

// Data callback mechanism: use cases register a parser, HTTP completion invokes it.

static data_callback_fn data_callback = NULL;
static void *data_callback_arg = NULL;

void set_data_callback(data_callback_fn callback, void *arg) {
    data_callback = callback;
    data_callback_arg = arg;
}

void http_invoke_data_callback(const char *data, size_t length, void *arg) {
    if (data_callback)
        data_callback(data, length, arg);
}

void http_default_completion(const char *body, size_t length, bool success, void *arg) {
    (void)arg;
    sync_operation_complete = true;
    sync_operation_success = success;

    debug_log("[DATA] HTTP completion: success=%s, body=%p, length=%zu\n", success ? "YES" : "NO",
              body, length);

    if (data_callback) {
        if (success && body && length > 0)
            data_callback(body, length, data_callback_arg);
        else
            data_callback(NULL, 0, data_callback_arg);
    }
}

static void http_send_telemetry(float battery_voltage, float coin_cell_voltage, bool query_ok) {
    const char *label = NULL;
    if (device_config_flash.data.roomname[0] != '\0') {
        label = device_config_flash.data.roomname;
    }

    inki_monitor_sample_t sample = {
        .battery_before_wifi_v = battery_voltage,
        .battery_after_wifi_v = read_battery_voltage(device_config_flash.data.conversion_factor),
        .coin_cell_v = coin_cell_voltage,
        .pico_temp_c = read_onchip_temperature_c(),
        .telemetry_send_elapsed_ms = (uint32_t)to_ms_since_boot(get_absolute_time()),
        .query_ok = query_ok,
        .label = label,
        .wake_source = wake_source,
    };
    (void)inki_monitor_send_sample(&sample);
}

WifiResult http_run_with_wifi(bool (*make_request)(void), float battery_voltage,
                              float coin_cell_voltage) {
    WifiResult wifi_result = wifi_connect();
    if (wifi_result != WIFI_SUCCESS) {
        return wifi_result;
    }

    if (!make_request()) {
        http_send_telemetry(battery_voltage, coin_cell_voltage, false);
        wifi_log_rssi();
        cyw43_arch_deinit();
        return WIFI_ERROR_SERVER;
    }

    // Wait for completion with timeout
    int max_waits = 0;
    dlog("Waiting for HTTP response...\n");
    while (!sync_operation_complete && max_waits < device_config_flash.data.max_wait_data_wifi) {
        sleep_ms(50);
        max_waits++;
    }

    if (!sync_operation_complete) {
        debug_status("ERROR", "HTTP request timeout\n");
        http_send_telemetry(battery_voltage, coin_cell_voltage, false);
        wifi_log_rssi();
        cyw43_arch_deinit();
        return WIFI_ERROR_SERVER;
    }

    if (!sync_operation_success) {
        debug_status("ERROR", "HTTP request failed\n");
        http_send_telemetry(battery_voltage, coin_cell_voltage, false);
        wifi_log_rssi();
        cyw43_arch_deinit();
        return WIFI_ERROR_SERVER;
    }

    http_send_telemetry(battery_voltage, coin_cell_voltage, true);
    wifi_log_rssi();
    dlog("Response complete, Wi-Fi off\n");
    cyw43_arch_deinit();

    return WIFI_SUCCESS;
}

bool http_get_server_time(rtc_time_t *out) {
    if (!g_server_time_valid || !out) {
        return false;
    }
    *out = g_server_time;
    return true;
}
