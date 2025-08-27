/**
 * @file http_client.c
 * @brief Robust HTTP client with chunked transfer support
 * 
 * This module provides a robust HTTP client implementation based on the historian
 * branch architecture. It supports:
 * - Dynamic memory allocation based on Content-Length
 * - Proper HTTP header/body separation
 * - Fallback mode for responses without Content-Length (SeatSurfing compatibility)
 * - Session-based connection management
 * - Async callback processing ready for historian integration
 * 
 * The module maintains full backward compatibility with the original SeatSurfing
 * implementation while providing enhanced reliability and scalability.
 */

#include "http_client.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Global session (single session for now, can be extended later)
static http_session_t g_session = {0};
static bool g_transfer_was_successful = false;

// Global response access (for main.c integration)
char* g_http_response_body = NULL;
size_t g_http_response_length = 0;

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

        // Handle fallback mode (no Content-Length) - connection close means we're done
        if (session->active && !session->transfer_complete && session->header_complete && session->expected_length == 0) {
            // In fallback mode, we use the header buffer as our response
            char* body_start = strstr(session->header_buffer, "\r\n\r\n");
            if (body_start) {
                body_start += 4;  // Skip past "\r\n\r\n"
                size_t response_len = strlen(body_start);
                if (response_len > 0) {
                    // Fallback mode complete with response data
                    session->transfer_complete = true;
                    if (session->completion_callback) {
                        session->completion_callback(body_start, response_len, true, session->callback_arg);
                    }
                    reset_session(session);
                    return ERR_OK;
                }
            }
        }

        // Only call callback if transfer was NOT complete (error case)
        if (session->active && !session->transfer_complete && session->completion_callback) {
            session->completion_callback(NULL, 0, false, session->callback_arg);
        }
        reset_session(session);
        return ERR_OK;
    }

    // Buffer for current chunk
    char chunk[1500];
    int copied = pbuf_copy_partial(p, chunk, sizeof(chunk), 0);

    // === Phase 1: Header collection ===
    if (!session->header_complete) {
        size_t space = sizeof(session->header_buffer) - session->header_length - 1;
        size_t to_copy = (copied < space) ? copied : space;

        memcpy(session->header_buffer + session->header_length, chunk, to_copy);
        session->header_length += to_copy;
        session->header_buffer[session->header_length] = '\0';

        debug_log("[HTTP] Header chunk: %d bytes (total: %d)\n",
                  copied, (int)session->header_length);

        // Check for header end
        char* header_end = strstr(session->header_buffer, "\r\n\r\n");
        if (header_end) {
            session->header_complete = true;
            session->state = HTTP_SESSION_RECEIVING_BODY;
            header_end += 4;  // Skip past "\r\n\r\n"

            // Parse Content-Length
            int content_length = parse_content_length(session->header_buffer);
            if (content_length <= 0) {
                // No Content-Length - use fallback mode for compatibility
                
                // Fallback: Use header + any body data received so far as complete response
                // This matches the original implementation's behavior
                
                // Check if there's body data in the header packet  
                char* body_start = strstr(session->header_buffer, "\r\n\r\n");
                if (body_start) {
                    body_start += 4;  // Skip past "\r\n\r\n"
                    size_t body_in_header = session->header_length - (body_start - session->header_buffer);
                    
                    if (body_in_header > 0) {
                        // Found body data in header packet - complete response
                        
                        // Body data is already in the header buffer, no need to copy
                        
                        session->transfer_complete = true;
                        session->state = HTTP_SESSION_COMPLETE;
                        
                        if (session->completion_callback) {
                            session->completion_callback(body_start, body_in_header, true, session->callback_arg);
                        }
                        
                        altcp_recved(pcb, p->tot_len);
                        pbuf_free(p);
                        altcp_close(pcb);
                        session->pcb = NULL;
                        return ERR_OK;
                    }
                }
                
                // If no body yet, wait for more data (fallback mode)
                session->expected_length = 0;  // Signal fallback mode
                altcp_recved(pcb, p->tot_len);
                pbuf_free(p);
                return ERR_OK;
            }

            session->expected_length = content_length;
            debug_log("[HTTP] Content-Length: %d bytes\n", content_length);

            // Allocate body buffer
            session->body_buffer = malloc(content_length + 1);
            if (!session->body_buffer) {
                debug_log_with_color(COLOR_RED,
                                     "[HTTP] Failed to allocate %d bytes for body\n",
                                     content_length);
                altcp_recved(pcb, p->tot_len);
                pbuf_free(p);
                altcp_close(pcb);
                session->state = HTTP_SESSION_ERROR;
                session->last_error = ERR_MEM;
                if (session->completion_callback) {
                    session->completion_callback(NULL, 0, false, session->callback_arg);
                }
                reset_session(session);
                return ERR_MEM;
            }
            session->body_buffer_size = content_length + 1;
            session->total_received = 0;

            // Copy any body data that was in the header packet
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

        altcp_recved(pcb, p->tot_len);
        pbuf_free(p);

        // Check if we already have all data
        if (session->header_complete &&
            session->total_received >= session->expected_length) {
            goto transfer_complete;
        }

        return ERR_OK;
    }

    // === Phase 2: Body reception ===
    if (session->active && session->header_complete) {
        // Handle fallback mode (no Content-Length)
        if (session->expected_length == 0) {
            // In fallback mode, extend the header buffer to include more data
            size_t remaining_space = sizeof(session->header_buffer) - session->header_length - 1;
            size_t to_copy = (copied < remaining_space) ? copied : remaining_space;
            
            if (to_copy > 0) {
                memcpy(session->header_buffer + session->header_length, chunk, to_copy);
                session->header_length += to_copy;
                session->header_buffer[session->header_length] = '\0';
            }
            
            altcp_recved(pcb, p->tot_len);
            pbuf_free(p);
            return ERR_OK;
        }
        
        // Normal mode with Content-Length
        size_t remaining = session->expected_length - session->total_received;
        size_t to_copy = (copied < remaining) ? copied : remaining;

        memcpy(session->body_buffer + session->total_received, chunk, to_copy);
        session->total_received += to_copy;

        // Progress logging every 10%
        static int last_percent = -10;
        int percent = (session->total_received * 100) / session->expected_length;
        if (percent >= last_percent + 10) {
            debug_log("[HTTP] Progress: %d%% (%d/%d bytes)\n",
                      percent, (int)session->total_received,
                      (int)session->expected_length);
            last_percent = percent;
        }

        altcp_recved(pcb, p->tot_len);
        pbuf_free(p);

        // Check if transfer complete
        if (session->total_received >= session->expected_length) {
            goto transfer_complete;
        }

        return ERR_OK;
    }

    // Should not reach here
    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);
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
    g_http_response_body = session->body_buffer;
    g_http_response_length = session->expected_length;

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
    g_http_response_body = NULL;
    g_http_response_length = 0;
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

void http_session_cleanup(http_session_t* session) {
    if (session->pcb) {
        altcp_close(session->pcb);
    }
    reset_session(session);
}

bool http_session_is_active(void) {
    return g_session.active;
}

// === SeatSurfing Compatibility Layer ===

/**
 * This section maintains full backward compatibility with the original SeatSurfing
 * implementation while using the new robust HTTP client internally.
 * 
 * Key compatibility features:
 * - wifi_server_communication() function with identical API
 * - Same WifiResult return codes  
 * - Global server_response_buf access for parse_seat_info()
 * - Identical error handling and display behavior
 */

// Include required headers for SeatSurfing integration
#include "wifi.h"
#include "base64.h" 
#include "pico/cyw43_arch.h"
#include "lwip/netif.h"
#include "flash.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"

// WifiResult enum now defined in http_client.h

// Global compatibility buffers - for parse_seat_info compatibility  
static char server_response_buf[8192];  // Larger buffer for robustness

// Synchronous operation tracking for SeatSurfing compatibility
static bool sync_operation_complete = false;
static bool sync_operation_success = false;

/**
 * @brief Completion callback for synchronous HTTP operations
 * @param body Response body data
 * @param length Response body length
 * @param success Operation success flag
 * @param arg User argument (unused)
 * 
 * Copies successful response data to compatibility buffer for parse_seat_info().
 */
static void sync_completion_callback(const char* body, size_t length, bool success, void* arg) {
    sync_operation_complete = true;
    sync_operation_success = success;
    
    if (success && body && length > 0) {
        // Copy to compatibility buffer for parse_seat_info
        size_t copy_len = length < sizeof(server_response_buf) - 1 ? length : sizeof(server_response_buf) - 1;
        memcpy(server_response_buf, body, copy_len);
        server_response_buf[copy_len] = '\0';
    }
}

/**
 * @brief SeatSurfing server communication (compatibility function)
 * @param voltage Battery voltage (for transmission to server)
 * @return WifiResult status code
 * 
 * This function maintains the exact same API and behavior as the original
 * implementation while using the new robust HTTP client internally.
 * 
 * Flow:
 * 1. Initialize Wi-Fi and connect to network
 * 2. Build HTTP Basic Auth request for SeatSurfing API  
 * 3. Send request using robust HTTP client
 * 4. Wait for response and handle errors
 * 5. Return appropriate WifiResult for main.c processing
 * 
 * The response data is accessible via get_server_response_buf() for
 * compatibility with existing parse_seat_info() function.
 */
WifiResult wifi_server_communication(float voltage) {
    // Clear compatibility buffer
    memset(server_response_buf, 0, sizeof(server_response_buf));
    
    debug_log_with_color(COLOR_BOLD_GREEN, "Initialization of Wi-Fi [switching cyw43 module on]...\n");
    if (cyw43_arch_init_with_country(country)) {
        debug_log_with_color(COLOR_RED, "Wi-Fi initialization failed.\n");
        return WIFI_ERROR_CONNECTION;
    }
    cyw43_arch_enable_sta_mode();
    
    if (device_config_flash.data.roomname != NULL) {
        netif_set_hostname(netif_default, device_config_flash.data.roomname);
    }
    
    debug_log("Attempt to connect to the specified network...\n");
    int wifi_connected = -1;
    int wifi_attempt_count = 0;
    while (wifi_connected != 0 && wifi_attempt_count < device_config_flash.data.number_wifi_attempts) {
        wifi_attempt_count++;
        wifi_connected = cyw43_arch_wifi_connect_timeout_ms(
            wifi_config_flash.ssid,
            wifi_config_flash.password,
            auth,
            device_config_flash.data.wifi_timeout
        );
        watchdog_update();
        debug_log_with_color(COLOR_YELLOW, "Trying to connect to %s ... Attempt %d\n", wifi_config_flash.ssid, wifi_attempt_count);
    }
    
    if (wifi_connected != 0) {
        debug_log_with_color(COLOR_RED, "Failed to connect to Wi-Fi after %d attempts.\n", wifi_attempt_count);
        cyw43_arch_deinit();
        return WIFI_ERROR_CONNECTION;
    }
    
    debug_log("Connected to Wi-Fi successfully.\n");

    // Build "username:password" string for HTTP Basic Auth
    char userpass[128];
    snprintf(userpass, sizeof(userpass), "%s:%s", seatsurfing_config_flash.data.username, seatsurfing_config_flash.data.password);
    
    // Encode the userpass string to Base64
    char auth_b64[192];
    base64_encode(userpass, strlen(userpass), auth_b64, sizeof(auth_b64));
    
    // Construct HTTP/1.0 request
    char header[1024];
    snprintf(header, sizeof(header),
            "GET /location/%s/space/%s/availability HTTP/1.0\r\n"
            "Host: %s\r\n"
            "Authorization: Basic %s\r\n"
            "\r\n",
            seatsurfing_config_flash.data.location_id,
            seatsurfing_config_flash.data.space_id,
            seatsurfing_config_flash.data.host,
            auth_b64
    );

    debug_log("Constructed HTTP Header:\n%s\n", header);

    // Setup IP address
    ip_addr_t ip;
    IP4_ADDR(&ip,
             seatsurfing_config_flash.data.ip[0],
             seatsurfing_config_flash.data.ip[1],
             seatsurfing_config_flash.data.ip[2],
             seatsurfing_config_flash.data.ip[3]);

    // Make HTTP request using new robust client
    sync_operation_complete = false;
    sync_operation_success = false;
    
    http_result_t result = http_request_async(&ip, seatsurfing_config_flash.data.port, 
                                             header, sync_completion_callback, NULL);
    
    if (result != HTTP_SUCCESS) {
        debug_log_with_color(COLOR_RED, "HTTP request failed to start: %d\n", result);
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

/**
 * @brief Get server response buffer for parse_seat_info compatibility
 * @return Pointer to response buffer containing JSON data
 * 
 * Provides access to the response buffer for the existing parse_seat_info()
 * function, maintaining full backward compatibility.
 */
char* get_server_response_buf(void) {
    return server_response_buf;
}