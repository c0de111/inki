/**
 * @file http_client.c
 * @brief HTTP client with chunked transfer support
 * 
 * This module provides a HTTP client implementation. It supports:
 * - Dynamic memory allocation based on Content-Length
 * - HTTP header/body separation
 * - Fallback mode for responses without Content-Length
 * - Session-based connection management
 * - Async callback processing ready for historian integration
 * 
 */

// All required headers (no conditional compilation for includes)
#include "http_client.h"
#include "debug.h"
#include "wifi.h"
#include "base64.h"
#include "flash.h"
#include "historian_config.h"
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
 * 3. **HTTP Transfer**: Robust HTTP client handles connection, headers, chunked transfer
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

#ifdef USE_CASE_HISTORIAN

// Forward declaration for DST function from main.c
extern bool is_dst_europe(const ds3231_data_t* t);

/**
 * @brief Convert RTC time (MEZ/MESZ) to Unix timestamp (ms UTC)
 * @param rtc_time RTC data structure (always in local time MEZ/MESZ)
 * @return Unix timestamp in milliseconds UTC
 */
uint64_t historian_rtc_to_unix_ms(const ds3231_data_t* rtc_time) {
    struct tm timeinfo = {0};
    timeinfo.tm_year = rtc_time->year + 100;  // tm_year is years since 1900
    timeinfo.tm_mon = rtc_time->month - 1;    // tm_mon is 0-11
    timeinfo.tm_mday = rtc_time->date;
    timeinfo.tm_hour = rtc_time->hours;
    timeinfo.tm_min = rtc_time->minutes;
    timeinfo.tm_sec = rtc_time->seconds;
    timeinfo.tm_isdst = 0;  // RTC has no DST info

    // Set timezone to MEZ for mktime
    setenv("TZ", "CET-1", 1);  // MEZ = UTC+1
    tzset();

    // mktime now interprets timeinfo as MEZ and returns UTC
    time_t unix_seconds = mktime(&timeinfo);

    // Convert to milliseconds
    return (uint64_t)unix_seconds * 1000ULL;
}

/**
 * @brief Get current time as Unix timestamp (ms UTC)
 */
uint64_t historian_get_current_unix_ms(ds3231_t* clock) {
    ds3231_data_t current_time;
    ds3231_read_current_time(clock, &current_time);
    return historian_rtc_to_unix_ms(&current_time);
}

/**
 * @brief Get timestamp for X hours ago
 */
uint64_t historian_get_unix_ms_hours_ago(ds3231_t* clock, int hours) {
    uint64_t now_ms = historian_get_current_unix_ms(clock);
    uint64_t hours_in_ms = (uint64_t)hours * 3600ULL * 1000ULL;
    return now_ms - hours_in_ms;
}

/**
 * @brief Convert Unix timestamp to readable string (for debug)
 */
void historian_unix_ms_to_local_string(uint64_t unix_ms, char* buffer, size_t size) {
    time_t unix_seconds = unix_ms / 1000;
    struct tm* timeinfo = gmtime(&unix_seconds);

    // Simple DST rule for Europe
    bool is_dst = false;
    if (timeinfo->tm_mon >= 2 && timeinfo->tm_mon <= 9) {  // March(2) to October(9)
        is_dst = true;
    }

    // UTC to MEZ/MESZ
    time_t local_seconds = unix_seconds + 3600;  // +1h for MEZ
    if (is_dst) {
        local_seconds += 3600;  // +1h additional for MESZ
    }

    struct tm* local_time = gmtime(&local_seconds);

    snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d %s",
             local_time->tm_year + 1900,
             local_time->tm_mon + 1,
             local_time->tm_mday,
             local_time->tm_hour,
             local_time->tm_min,
             local_time->tm_sec,
             is_dst ? "MESZ" : "MEZ");
}

/**
 * @brief Build JSON-RPC request for historian server
 * @param buffer Output buffer for HTTP request
 * @param buffer_size Size of output buffer
 * @param host Historian server host
 * @param datapoint_id ID of datapoint to query
 * @param start_time_ms Start time in milliseconds (Unix timestamp)
 * @param end_time_ms End time in milliseconds (Unix timestamp)
 * @return Length of request on success, -1 on error
 */
static int historian_build_http_request(char* buffer, size_t buffer_size,
                                       const char* host,
                                       int datapoint_id,
                                       uint64_t start_time_ms,
                                       uint64_t end_time_ms) {
    // Build JSON-RPC body (using exact esign format without "jsonrpc":"2.0")
    static char json_body[512];
    int json_len = snprintf(json_body, sizeof(json_body),
                           "{"
                           "\"id\":%d,"
                           "\"method\":\"getTimeSeries\","
                           "\"params\":[%d,%llu,%llu]"
                           "}",
                           123,  // Request-ID (could be dynamic)
                           datapoint_id, start_time_ms, end_time_ms);

    if (json_len >= sizeof(json_body)) {
        debug_log_with_color(COLOR_RED, "[HISTORIAN] JSON body too large\n");
        return -1;
    }

    // Build complete HTTP request
    int request_len = snprintf(buffer, buffer_size,
                              "POST /query/jsonrpc.gy HTTP/1.0\r\n"
                              "Host: %s\r\n"
                              "Content-Type: application/json\r\n"
                              "Content-Length: %d\r\n"
                              "\r\n"
                              "%s",
                              host, json_len, json_body);

    if (request_len >= buffer_size) {
        debug_log_with_color(COLOR_RED, "[HISTORIAN] HTTP request too large\n");
        return -1;
    }

    return request_len;
}

#endif // USE_CASE_HISTORIAN

// =============================================================================
// SHARED HELPER FUNCTIONS
// =============================================================================

/**
 * @brief Initialize Wi-Fi and connect to network
 * @param use_case_name Use case name for logging (e.g., "HISTORIAN", "SEATSURFING")
 * @return WifiResult status code
 */
static WifiResult wifi_connect() {
    debug_log_with_color(COLOR_BOLD_GREEN, "Initializing Wi-Fi...\n");
    
    if (cyw43_arch_init_with_country(country)) {
        debug_log_with_color(COLOR_RED, "Wi-Fi initialization failed.\n");
        return WIFI_ERROR_CONNECTION;
    }
    cyw43_arch_enable_sta_mode();

    if (device_config_flash.data.roomname != NULL) {
        netif_set_hostname(netif_default, device_config_flash.data.roomname);
    }

    debug_log("Attempting to connect to network...\n");
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
        debug_log_with_color(COLOR_YELLOW, "Trying to connect to %s ... Attempt %d\n",
                            wifi_config_flash.ssid, wifi_attempt_count);
    }

    if (wifi_connected != 0) {
        debug_log_with_color(COLOR_RED, "Failed to connect to Wi-Fi after %d attempts.\n", wifi_attempt_count);
        cyw43_arch_deinit();
        return WIFI_ERROR_CONNECTION;
    }

    debug_log("Connected to Wi-Fi successfully.\n");
    return WIFI_SUCCESS;
}

#ifdef USE_CASE_HISTORIAN

/**
 * @brief Make complete historian HTTP request with full encapsulation
 * @return true on success (request sent), false on error
 */
static bool historian_make_request(void) {
    // Helper manages its own buffer
    static char http_request[1024];
    
    // TODO: Get from historian config
    const char* historian_host = "192.168.178.42";  // Default for testing
    int datapoint_id = 75;                          // Default temperature sensor
    int hours_back = 24;                           // Last 24 hours

    // Calculate time window using inki's RTC
    extern ds3231_t ds3231;  // Global RTC instance from main.c
    uint64_t end_time = historian_get_current_unix_ms(&ds3231); // from now to
    uint64_t start_time = historian_get_unix_ms_hours_ago(&ds3231, hours_back); // hours_back

    // Debug time window (commented out for production)
    // char time_str[64];
    // historian_unix_ms_to_local_string(start_time, time_str, sizeof(time_str));
    // debug_log("[HISTORIAN] Start time: %s (%llu ms)\n", time_str, start_time);
    // historian_unix_ms_to_local_string(end_time, time_str, sizeof(time_str));
    // debug_log("[HISTORIAN] End time: %s (%llu ms)\n", time_str, end_time);

    int request_len = historian_build_http_request(http_request, sizeof(http_request),
                                                  historian_host, datapoint_id, 
                                                  start_time, end_time);
    
    if (request_len < 0) {
        debug_log_with_color(COLOR_RED, "[HISTORIAN] Failed to build HTTP request\n");
        return false;
    }

    debug_log("[HISTORIAN] Constructed HTTP request:\n%s\n", http_request);
    watchdog_update();

    // Set up IP address (historian-specific)
    ip_addr_t ip;
    IP4_ADDR(&ip, 192, 168, 178, 42);  // Default historian server IP

    sync_operation_complete = false;
    sync_operation_success = false;

    // Make HTTP request (unified callback)
    http_result_t result = http_request_async(&ip, 81, http_request, 
                                             unified_completion_callback, NULL);
    
    if (result != HTTP_SUCCESS) {
        debug_log_with_color(COLOR_RED, "[HISTORIAN] HTTP request failed to start: %d\n", result);
        return false;
    }

    return true;  // Request started successfully
}

#endif // USE_CASE_HISTORIAN

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


#ifdef USE_CASE_HISTORIAN

// =============================================================================
// HISTORIAN JSON-RPC RESPONSE PARSING
// =============================================================================


/**
 * @brief Parse JSON-RPC response into TimeSeries structure
 * @param json JSON response from historian server
 * @param result Output TimeSeries structure
 * @return true on success, false on error
 */
bool historian_parse_timeseries(const char* json, TimeSeries* result) {
    if (!json || !result) return false;

    cJSON* root = cJSON_Parse(json);
    if (!root) {
        debug_log("[HISTORIAN] JSON parse failed\n");
        return false;
    }

    cJSON* res = cJSON_GetObjectItem(root, "result");
    if (!res) {
        debug_log("[HISTORIAN] No 'result' in JSON\n");
        cJSON_Delete(root);
        return false;
    }

    // Parse dataPoint metadata if available
    cJSON* dataPoint = cJSON_GetObjectItem(res, "dataPoint");
    if (dataPoint) {
        cJSON* dp_id = cJSON_GetObjectItem(dataPoint, "id");
        if (dp_id) {
            cJSON* interfaceId = cJSON_GetObjectItem(dp_id, "interfaceId");
            if (interfaceId && cJSON_IsString(interfaceId)) {
                strncpy(result->name, interfaceId->valuestring, sizeof(result->name) - 1);
            }
        }

        cJSON* attributes = cJSON_GetObjectItem(dataPoint, "attributes");
        if (attributes) {
            cJSON* unit = cJSON_GetObjectItem(attributes, "unit");
            if (unit && cJSON_IsString(unit)) {
                strncpy(result->unit, unit->valuestring, sizeof(result->unit) - 1);
            }
        }
    }

    // Parse timestamps and values arrays
    cJSON* timestamps = cJSON_GetObjectItem(res, "timestamps");
    cJSON* values = cJSON_GetObjectItem(res, "values");
    cJSON* states = cJSON_GetObjectItem(res, "states");  // Optional states array

    if (!timestamps || !values) {
        debug_log("[HISTORIAN] Missing timestamps or values\n");
        cJSON_Delete(root);
        return false;
    }

    int count = cJSON_GetArraySize(timestamps);
    if (count > MAX_DATA_POINTS) count = MAX_DATA_POINTS;

    for (int i = 0; i < count; i++) {
        cJSON* ts = cJSON_GetArrayItem(timestamps, i);
        cJSON* val = cJSON_GetArrayItem(values, i);

        if (ts && val) {
            // Store in DataPoint structure
            result->points[i].timestamp = (uint64_t)cJSON_GetNumberValue(ts);
            result->points[i].value = (float)cJSON_GetNumberValue(val);

            // Parse state if available
            if (states) {
                cJSON* state = cJSON_GetArrayItem(states, i);
                if (state) {
                    result->points[i].state = (uint8_t)cJSON_GetNumberValue(state);
                } else {
                    result->points[i].state = 0;  // Default: good
                }
            } else {
                result->points[i].state = 0;  // Default: good
            }
        }
    }

    result->count = count;

    // Calculate min/max/last values
    if (count > 0) {
        result->min_value = result->max_value = result->points[0].value;
        for (int i = 1; i < count; i++) {
            if (result->points[i].value < result->min_value)
                result->min_value = result->points[i].value;
            if (result->points[i].value > result->max_value)
                result->max_value = result->points[i].value;
        }
        result->last_value = result->points[count - 1].value;
    }

    cJSON_Delete(root);

    debug_log("[HISTORIAN] Parsed %d data points (min=%.2f, max=%.2f)\n",
              count, result->min_value, result->max_value);

    return true;
}

/**
 * @brief Prepare time series data for display (downsampling if needed)
 * @param series Time series data to process
 * @param target_points Target number of points for display
 */
void historian_prepare_display_data(TimeSeries* series, int target_points) {
    if (!series || series->count <= target_points) {
        return; // No downsampling needed
    }
    
    debug_log("historian_prepare_display_data: downsampling %d points to %d\n", 
              series->count, target_points);
    
    // Simple downsampling: take every nth point
    int step = series->count / target_points;
    if (step < 2) step = 2;
    
    int new_count = 0;
    for (int i = 0; i < series->count && new_count < target_points; i += step) {
        if (new_count < i) {
            series->points[new_count] = series->points[i];
        }
        new_count++;
    }
    
    // Always keep the last point
    if (new_count < target_points && series->count > 0) {
        series->points[new_count] = series->points[series->count - 1];
        new_count++;
    }
    
    series->count = new_count;
    debug_log("historian_prepare_display_data: result has %d points\n", new_count);
}

#endif // USE_CASE_HISTORIAN
