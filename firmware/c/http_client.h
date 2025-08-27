#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "lwip/altcp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "lwip/err.h"
#include "wifi.h"

// HTTP session states
typedef enum {
    HTTP_SESSION_INACTIVE,
    HTTP_SESSION_CONNECTING,
    HTTP_SESSION_CONNECTED,
    HTTP_SESSION_SENDING,
    HTTP_SESSION_RECEIVING_HEADER,
    HTTP_SESSION_RECEIVING_BODY,
    HTTP_SESSION_COMPLETE,
    HTTP_SESSION_ERROR
} http_session_state_t;

// HTTP session structure (based on historian architecture)
typedef struct {
    bool active;
    http_session_state_t state;
    bool header_complete;
    bool transfer_complete;
    
    // Header processing
    char header_buffer[2048];
    size_t header_length;
    
    // Body processing - dynamic allocation
    char* body_buffer;
    size_t body_buffer_size;
    size_t expected_length;
    size_t total_received;
    
    // Request data
    char* request_data;
    size_t request_length;
    
    // Connection management
    struct altcp_pcb* pcb;
    
    // Callback for completion
    void (*completion_callback)(const char* body, size_t length, bool success, void* arg);
    void* callback_arg;
    
    // Error tracking
    err_t last_error;
    
} http_session_t;

// Result codes for HTTP operations
typedef enum {
    HTTP_SUCCESS = 0,
    HTTP_ERROR_CONNECTION,
    HTTP_ERROR_MEMORY,
    HTTP_ERROR_TIMEOUT,
    HTTP_ERROR_INVALID_RESPONSE,
    HTTP_ERROR_BUFFER_OVERFLOW
} http_result_t;

// WifiResult type is defined in wifi.h

// === Public API ===

// Initialize HTTP client system
bool http_client_init(void);

// SeatSurfing compatibility function
WifiResult wifi_server_communication(float voltage);

// Main HTTP request function - handles both SeatSurfing and historian
http_result_t http_request_async(const ip_addr_t* server_ip, uint16_t port, 
                                const char* request_data,
                                void (*callback)(const char* body, size_t length, bool success, void* arg),
                                void* callback_arg);

// Synchronous wrapper for simple use cases
http_result_t http_request_sync(const ip_addr_t* server_ip, uint16_t port,
                               const char* request_data,
                               char** response_body, size_t* response_length);

// Session management
void http_session_cleanup(http_session_t* session);
bool http_session_is_active(void);

// Global access to last response (for current main.c integration)
extern char* g_http_response_body;
extern size_t g_http_response_length;

// SeatSurfing compatibility - access to response buffer for parse_seat_info
extern char* get_server_response_buf(void);

#endif // HTTP_CLIENT_H