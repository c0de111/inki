/**
 * @file http_client.c
 * @brief Minimal HTTP/1.0 client (no chunked TE)
 * 
 * This module provides a HTTP client implementation. It supports:
 * - Dynamic body accumulation with Content-Length
 * - HTTP header/body separation
 * - Fallback mode for responses without Content-Length (connection-close)
 * - Session-based connection management
 * - Async callback processing ready for historian integration
 * - Transfer-Encoding: chunked is NOT supported (explicitly rejected)
 * 
 */

// All required headers (no conditional compilation for includes)
#include "http_client.h"
#include "debug.h"
#include "wifi.h"
#include "base64.h"
#include "flash.h"
#include "historian_config.h"
#include "historian_client.h"
#include "seatsurfing_client.h"
#include "cJSON.h"

// Pico SDK headers
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"

// lwIP headers
#include "lwip/netif.h"

// Standard C headers
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include <time.h>
#include <limits.h>

// Global session (single session for now, can be extended later)
static http_session_t g_session = {0};
static bool g_transfer_was_successful = false;


// Synchronous operation tracking for both SeatSurfing and historian compatibility
static bool sync_operation_complete = false;
static bool sync_operation_success = false;

// === Helper Functions ===

/**
 * @brief Reset HTTP session to initial state
 * @param session Session structure to reset
 * 
 * Cleans up all allocated memory, resets state variables, and prepares
 * the session for reuse. Safe to call multiple times.
 */

static void reset_session(http_session_t* session) {
    session->active = false;
    session->state = HTTP_SESSION_INACTIVE;
    session->header_complete = false;
    session->transfer_complete = false;
    session->fallback_mode = false;
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
}

/**
 * @brief Parse Content-Length from HTTP header
 * @param header HTTP header string to parse
 * @return Content-Length value or -1 if not found
 * 
 * Searches for Content-Length header (case-insensitive) and extracts
 * the numeric value. Returns -1 if header is missing or invalid.
 */
static int parse_content_length(const char* header) {
    const char* cl = strstr(header, "Content-Length:");
    if (!cl) {
        cl = strstr(header, "content-length:");  // Case-insensitive
    }
    if (!cl) return -1;

    cl += 15;  // Skip "Content-Length:"
    while (*cl == ' ') cl++;  // Skip spaces

    return atoi(cl);
}

/**
 * @brief Parse HTTP status code from the status line
 * @param header HTTP header string
 * @return status code (e.g., 200) or -1 if not found
 */
static int parse_http_status_code(const char* header) {
    const char* p = strstr(header, "HTTP/");
    if (!p) return -1;
    // Find first space after HTTP/version
    p = strchr(p, ' ');
    if (!p) return -1;
    // Skip spaces and parse number
    while (*p == ' ') p++;
    return atoi(p);
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
static err_t http_recv_callback(void* arg, struct altcp_pcb* pcb, struct pbuf* p, err_t err) {
    http_session_t* session = (http_session_t*)arg;
    
    if (!p) {
        // Connection closed
        debug_log("[HTTP] Connection closed by server\n");
        altcp_close(pcb);
        session->pcb = NULL;
        
        // Handle fallback mode (no Content-Length): connection close marks completion
        if (session->active && !session->transfer_complete && session->header_complete && session->fallback_mode) {
            if (session->body_buffer && session->total_received > 0) {
                session->body_buffer[session->total_received] = '\0';
                session->transfer_complete = true;
                g_transfer_was_successful = true;
                if (session->completion_callback) {
                    session->completion_callback(session->body_buffer, session->total_received, true, session->callback_arg);
                }
                reset_session(session);
                return ERR_OK;
            }
        }

        // Only call callback if transfer was NOT complete (error case)
        if (session->active && !session->transfer_complete && session->completion_callback) {
            session->completion_callback(NULL, 0, false, session->callback_arg);
        }
        reset_session(session);
        return ERR_OK;
    }

    // Buffer for streaming slices
    char chunk[HTTP_RECV_SLICE];
    size_t offset = 0;
    
    while (offset < p->tot_len) {
        int step = pbuf_copy_partial(p, chunk, sizeof(chunk), offset);
        if (step <= 0) break;
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

            debug_log("[HTTP] Header chunk: %d bytes (total: %d)\n",
                      step, (int)session->header_length);

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
            char* header_end = strstr(session->header_buffer, "\r\n\r\n");
            if (header_end) {
                session->header_complete = true;
                session->state = HTTP_SESSION_RECEIVING_BODY;
                header_end += 4;  // Skip past CRLFCRLF

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
                const char* te1 = strstr(session->header_buffer, "Transfer-Encoding:");
                const char* te2 = te1 ? te1 : strstr(session->header_buffer, "transfer-encoding:");
                if (te2 && (strstr(te2, "chunked") || strstr(te2, "Chunked"))) {
                    debug_log_with_color(COLOR_RED, "[HTTP] Chunked transfer-encoding not supported\n");
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
                if (content_length < 0) {
                    // No Content-Length: start fallback dynamic accumulation
                    session->fallback_mode = true;
                    session->expected_length = 0;  // not used in fallback mode
                    session->body_buffer = NULL;
                    session->body_buffer_size = 0;
                    session->total_received = 0;

                    // Copy any body bytes already captured in header buffer
                    size_t body_in_header = session->header_length - (header_end - session->header_buffer);
                    if (body_in_header > 0) {
                        session->body_buffer = (char*)malloc(body_in_header + 1);
                        if (!session->body_buffer) {
                            debug_log_with_color(COLOR_RED, "[HTTP] Failed to allocate fallback body buffer\n");
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
                        memcpy(session->body_buffer, header_end, body_in_header);
                        session->total_received = body_in_header;
                        session->body_buffer[session->total_received] = '\0';
                        session->body_buffer_size = body_in_header + 1;
                        debug_log("[HTTP] Fallback: captured %d body bytes in header packet\n", (int)body_in_header);
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
                        debug_log_with_color(COLOR_RED, "[HTTP] Content-Length too large for platform\n");
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
                    debug_log("[HTTP] Content-Length: %u bytes\n", (unsigned)clen);

                    // Allocate body buffer
                    session->body_buffer = (char*)malloc(clen + 1);
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
                    session->total_received = 0;

                    // Copy any body data that was already captured in header buffer
                    size_t body_in_header = session->header_length - (header_end - session->header_buffer);
                    if (body_in_header > 0) {
                        if (body_in_header > session->expected_length) {
                            body_in_header = session->expected_length;
                        }
                        memcpy(session->body_buffer, header_end, body_in_header);
                        session->total_received = body_in_header;
                        debug_log("[HTTP] Found %d body bytes in header packet\n",
                                  (int)body_in_header);
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
                if (step > 0) {
                    size_t sstep = (size_t)step;
                    // Overflow guard for needed calculation
                    if (session->total_received > SIZE_MAX - (sstep + 1)) {
                        debug_log_with_color(COLOR_RED, "[HTTP] Body size overflow in fallback mode\n");
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
                        char* nb = (char*)realloc(session->body_buffer, new_cap);
                        if (!nb) {
                            debug_log_with_color(COLOR_RED, "[HTTP] realloc failed in fallback mode\n");
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
                    memcpy(session->body_buffer + session->total_received, chunk, to_copy);
                    session->total_received += to_copy;
                }

                // Progress logging every 10%
                static int last_percent = -10;
                int percent = (session->total_received * 100) / session->expected_length;
                if (percent >= last_percent + 10) {
                    debug_log("[HTTP] Progress: %d%% (%d/%d bytes)\n",
                              percent, (int)session->total_received,
                              (int)session->expected_length);
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
    session->body_buffer[session->expected_length] = '\0';
    session->transfer_complete = true;
    session->state = HTTP_SESSION_COMPLETE;

    debug_log_with_color(COLOR_GREEN,
                         "[HTTP] Transfer complete: %d bytes received\n",
                         (int)session->expected_length);

    // Set global flags
    g_transfer_was_successful = true;

    // Call callback with complete data
    if (session->completion_callback) {
        session->completion_callback(session->body_buffer, session->expected_length,
                                     true, session->callback_arg);
    }

    // Clean up connection
    altcp_close(pcb);
    session->pcb = NULL;

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
static err_t http_connected_callback(void* arg, struct altcp_pcb* pcb, err_t err) {
    http_session_t* session = (http_session_t*)arg;
    
    if (err != ERR_OK) {
        debug_log_with_color(COLOR_RED,
                             "[HTTP] Connection failed: %d\n", err);
        session->state = HTTP_SESSION_ERROR;
        session->last_error = err;
        return err;
    }

    debug_log("[HTTP] Connected to server\n");
    session->state = HTTP_SESSION_CONNECTED;

    // Send HTTP request
    err_t write_err = altcp_write(pcb, session->request_data, session->request_length, TCP_WRITE_FLAG_COPY);
    if (write_err != ERR_OK) {
        debug_log_with_color(COLOR_RED,
                             "[HTTP] Failed to send request: %d\n", write_err);
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

    altcp_output(pcb);
    session->state = HTTP_SESSION_SENDING;
    debug_log("[HTTP] Request sent (%d bytes)\n", (int)session->request_length);

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
static void http_error_callback(void* arg, err_t err) {
    http_session_t* session = (http_session_t*)arg;
    
    // Check if transfer was successful before error
    if (!g_transfer_was_successful) {
        debug_log_with_color(COLOR_RED,
                             "[HTTP] TCP error: %d (transfer incomplete)\n", err);

        session->state = HTTP_SESSION_ERROR;
        session->last_error = err;

        if (session->completion_callback) {
            session->completion_callback(NULL, 0, false, session->callback_arg);
        }
        // Ensure all buffers and state are cleaned up on error
        reset_session(session);
    } else {
        // Transfer was successful, connection close is normal
        debug_log("[HTTP] TCP connection closed normally after successful transfer\n");
    }

    session->pcb = NULL;
    g_transfer_was_successful = false;  // Reset for next transfer
}

// === Public API Implementation ===

bool http_client_init(void) {
    reset_session(&g_session);
    return true;
}

http_result_t http_request_async(const ip_addr_t* server_ip, uint16_t port,
                                const char* request_data,
                                void (*callback)(const char* body, size_t length, bool success, void* arg),
                                void* callback_arg) {
    // Reset any previous session
    reset_session(&g_session);
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

    debug_log("[HTTP] Creating TCP connection...\n");

    // Create TCP PCB
    g_session.pcb = altcp_new(NULL);
    if (!g_session.pcb) {
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
    if (err != ERR_OK) {
        debug_log_with_color(COLOR_RED,
                             "[HTTP] Failed to connect: %d\n", err);
        altcp_close(g_session.pcb);
        g_session.pcb = NULL;
        reset_session(&g_session);
        return HTTP_ERROR_CONNECTION;
    }

    return HTTP_SUCCESS;
}


bool http_session_is_active(void) {
    return g_session.active;
}

// === Historian Implementation ===

// =============================================================================
// UNIFIED CALLBACK SYSTEM FOR ALL USE CASES
// =============================================================================

/**
 * @section use_case_integration Use Case Integration Guide
 * 
 * This HTTP client supports multiple use cases through a unified callback architecture.
 * Each use case (SeatSurfing, Historian, Weather, etc.) follows the same integration pattern.
 * 
 * @subsection adding_use_case Adding a New Use Case
 * 
 * To add a new use case (e.g., Weather), implement these components:
 * 
 * @par 1. Configuration (config.h)
 * @code
 * #define USE_CASE_WEATHER    // Weather data display
 * @endcode
 * 
 * @par 2. Data Callback Function (main.c)
 * @code
 * void weather_data_received(const char* response_data, size_t length, void* arg) {
 *     // Parse response_data (JSON, XML, etc.)
 *     // Store in global variable for display functions
 *     // Handle errors gracefully
 * }
 * @endcode
 * 
 * @par 3. Request Builder Function (http_client.c)
 * @code
 * #ifdef USE_CASE_WEATHER
 * static bool weather_make_request(void) {
 *     // Build HTTP request (GET, POST, headers, etc.)
 *     // Set up IP address and port
 *     // Call http_request_async() with unified_completion_callback
 *     // Return success/failure
 * }
 * #endif
 * @endcode
 * 
 * @par 4. Communication Integration (http_client.c)
 * Add to wifi_server_communication():
 * @code
 * #elif defined(USE_CASE_WEATHER)
 *     if (!weather_make_request()) {
 * @endcode
 * 
 * @par 5. Callback Registration (main.c)
 * Add to main() Wi-Fi section:
 * @code
 * #elif defined(USE_CASE_WEATHER)
 *     set_data_callback(weather_data_received, NULL);
 * @endcode
 * 
 * @subsection communication_flow Communication Flow
 * 
 * 1. **Callback Registration**: main.c calls set_data_callback() with use-case specific function
 * 2. **Request Initiation**: wifi_server_communication() calls use-case specific make_request()
 * 3. **HTTP Transfer**: Robust HTTP client handles connection, headers, and body streaming (no chunked TE)
 * 4. **Data Processing**: unified_completion_callback() calls registered callback with response
 * 5. **Display Update**: Callback parses data and stores in global variables for display
 * 
 * @subsection architecture_benefits Architecture Benefits
 * 
 * - **Consistent Pattern**: All use cases follow identical integration steps
 * - **Automatic Processing**: Data parsed immediately when received via callbacks
 * - **Error Handling**: Unified timeout, retry, and cleanup logic for all use cases
 * - **Clean Separation**: HTTP communication vs data processing cleanly separated
 * - **Build Optimization**: Only active use case compiled, others excluded
 * 
 * @note The unified callback system replaces manual parsing after communication.
 *       Display functions should use parsed global data, not raw response buffers.
 */

// Universal callback type for all use cases
typedef void (*data_callback_fn)(const char* response_data, size_t length, void* arg);

// Unified callback mechanism for all use cases
static data_callback_fn data_callback = NULL;
static void* data_callback_arg = NULL;

/**
 * @brief Set callback function for data processing (unified API for all use cases)
 * @param callback Function to call when data is received
 * @param arg User argument to pass to callback
 */
void set_data_callback(data_callback_fn callback, void* arg) {
    data_callback = callback;
    data_callback_arg = arg;
    debug_log("[DATA] Callback set: %p (arg: %p)\n", callback, arg);
}

/**
 * @brief HTTP completion callback for all use cases
 * Calls the registered data callback with response data
 */
static void unified_completion_callback(const char* body, size_t length, bool success, void* arg) {
    sync_operation_complete = true;
    sync_operation_success = success;
    
    debug_log("[DATA] HTTP completion: success=%s, body=%p, length=%zu\n", 
              success ? "YES" : "NO", body, length);
    
    // Call registered callback if available and transfer successful
    if (data_callback) {
        if (success && body && length > 0) {
            debug_log("[DATA] Calling registered callback with data\n");
            data_callback(body, length, data_callback_arg);
        } else {
            debug_log("[DATA] Calling registered callback with NULL (error)\n");
            data_callback(NULL, 0, data_callback_arg);
        }
    } else {
        debug_log_with_color(COLOR_RED, "[DATA] No callback registered!\n");
    }
}

// (Historian-specific utilities moved to historian_client.*)

// =============================================================================
// SHARED HELPER FUNCTIONS
// =============================================================================

// Wi‑Fi connection logic moved to wifi.c (wifi_connect())

#ifdef USE_CASE_HISTORIAN

/**
 * @brief Make complete historian HTTP request with full encapsulation
 * @return true on success (request sent), false on error
 */
static bool historian_make_request(void) {
    // Helper manages its own buffer
    static char http_request[HTTP_REQUEST_MAX];
    
    // Get values from historian config
    char historian_host[16];  // "xxx.xxx.xxx.xxx" format
    snprintf(historian_host, sizeof(historian_host), "%d.%d.%d.%d",
             historian_config_flash.data.ip[0], historian_config_flash.data.ip[1],
             historian_config_flash.data.ip[2], historian_config_flash.data.ip[3]);
    int datapoint_id = historian_config_flash.data.datapoint_id;
    int hours_back = historian_config_flash.data.hours_back;

    // Calculate time window using inki's RTC
    extern ds3231_t ds3231;  // Global RTC instance from main.c
    uint64_t end_time = historian_get_current_unix_ms(&ds3231); // from now to
    uint64_t start_time = historian_get_unix_ms_hours_ago(&ds3231, hours_back); // hours_back

    int request_len = historian_build_http_request(http_request, sizeof(http_request),
                                                  historian_host, datapoint_id, 
                                                  start_time, end_time);
    
    if (request_len < 0) {
        debug_log_with_color(COLOR_RED, "[HISTORIAN] Failed to build HTTP request\n");
        return false;
    }

    debug_log("[HISTORIAN] Constructed HTTP request:\n%s\n", http_request);
    watchdog_update();

    // Set up IP address from config
    ip_addr_t ip;
    IP4_ADDR(&ip, historian_config_flash.data.ip[0], historian_config_flash.data.ip[1], 
             historian_config_flash.data.ip[2], historian_config_flash.data.ip[3]);

    sync_operation_complete = false;
    sync_operation_success = false;

    // Make HTTP request (unified callback)
    http_result_t result = http_request_async(&ip, historian_config_flash.data.port, http_request, 
                                             unified_completion_callback, NULL);
    
    if (result != HTTP_SUCCESS) {
        debug_log_with_color(COLOR_RED, "[HISTORIAN] HTTP request failed to start: %d\n", result);
        return false;
    }

    return true;  // Request started successfully
}

#endif // USE_CASE_HISTORIAN

#ifdef USE_CASE_SEATSURFING
/**
 * @brief Make complete SeatSurfing HTTP request with full encapsulation
 * @return true on success (request sent), false on error
 */
static bool seatsurfing_make_request(void) {
    // Build "username:password" string for HTTP Basic Auth
    char userpass[128];
    snprintf(userpass, sizeof(userpass), "%s:%s", seatsurfing_config_flash.data.username, seatsurfing_config_flash.data.password);
    
    // Encode the userpass string to Base64
    char auth_b64[192];
    base64_encode(userpass, strlen(userpass), auth_b64, sizeof(auth_b64));
    
    // Construct HTTP/1.0 request
    char header[HTTP_REQUEST_MAX];
    int hlen = seatsurfing_build_http_request(
        header, sizeof(header),
        seatsurfing_config_flash.data.host,
        seatsurfing_config_flash.data.location_id,
        seatsurfing_config_flash.data.space_id,
        auth_b64);
    if (hlen < 0) {
        debug_log_with_color(COLOR_RED, "[SEATSURFING] Failed to build HTTP request\n");
        return false;
    }

    debug_log("Constructed HTTP Header:\n%s\n", header);

    // Setup IP address
    ip_addr_t ip;
    IP4_ADDR(&ip,
             seatsurfing_config_flash.data.ip[0],
             seatsurfing_config_flash.data.ip[1],
             seatsurfing_config_flash.data.ip[2],
             seatsurfing_config_flash.data.ip[3]);

    // Make HTTP request using unified callback
    sync_operation_complete = false;
    sync_operation_success = false;
    
    http_result_t result = http_request_async(&ip, seatsurfing_config_flash.data.port,
                                              header, unified_completion_callback, NULL);
    
    if (result != HTTP_SUCCESS) {
        debug_log_with_color(COLOR_RED, "HTTP request failed to start: %d\n", result);
        return false;
    }

    return true;  // Request started successfully
}
#endif // USE_CASE_SEATSURFING

/**
 * @brief server communication
 * @param voltage Battery voltage (for transmission to server)
 * @return WifiResult status code
 * 
 * Flow:
 * 1. Initialize Wi-Fi and connect to network
 * 2. Build HTTP Basic Auth request for SeatSurfing API  
 * 3. Send request using robust HTTP client
 * 4. Wait for response and handle errors
 * 5. Return appropriate WifiResult for main.c processing
 * 
 * Response data is automatically processed via registered callbacks.
 */
WifiResult wifi_server_communication(float voltage) {
    
    // Initialize Wi-Fi and connect using shared function
    WifiResult wifi_result = wifi_connect();
    if (wifi_result != WIFI_SUCCESS) {
        return wifi_result;
    }

    // Make use-case specific request (fully encapsulated)
#ifdef USE_CASE_HISTORIAN
    if (!historian_make_request()) {
#elif defined(USE_CASE_SEATSURFING)
    if (!seatsurfing_make_request()) {
#endif
        cyw43_arch_deinit();
        return WIFI_ERROR_SERVER;
    }

    // Wait for completion with timeout
    int max_waits = 0;
    debug_log_with_color(COLOR_YELLOW, "Waiting for HTTP response: ");
    while (!sync_operation_complete && max_waits < device_config_flash.data.max_wait_data_wifi) {
        sleep_ms(50);
        debug_log_with_color(COLOR_YELLOW, ".");
        max_waits++;
    }
    
    debug_log("\n");
    
    if (!sync_operation_complete) {
        debug_log_with_color(COLOR_RED, "HTTP request timeout\n");
        cyw43_arch_deinit();
        return WIFI_ERROR_SERVER;
    }
    
    if (!sync_operation_success) {
        debug_log_with_color(COLOR_RED, "HTTP request failed or server returned error\n");
        cyw43_arch_deinit();
        return WIFI_ERROR_SERVER;
    }

    debug_log_with_color(COLOR_BOLD_GREEN, "✅ JSON response complete - Wi-Fi off.\n");
    cyw43_arch_deinit();
    
    return WIFI_SUCCESS;
}


// Historian JSON parsing and utilities moved to historian_client.c
