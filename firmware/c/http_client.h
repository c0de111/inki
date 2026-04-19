#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "lwip/altcp.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "rtc.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Tunable limits and buffer sizes
#ifndef HTTP_RECV_SLICE
#define HTTP_RECV_SLICE 1500 // bytes processed per pbuf slice (approx. MTU)
#endif

#ifndef HTTP_HEADER_MAX
#define HTTP_HEADER_MAX 4096 // max accumulated header size before CRLFCRLF
#endif

#ifndef HTTP_JSON_BODY_MAX
#define HTTP_JSON_BODY_MAX 512 // historian JSON-RPC body build buffer
#endif

#ifndef HTTP_REQUEST_MAX
#define HTTP_REQUEST_MAX 2048 // max HTTP request length we build (room for multicall)
#endif

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

// HTTP session structure
typedef struct {
    bool active;
    http_session_state_t state;
    bool header_complete;
    bool transfer_complete;
    bool fallback_mode; // true when response has no Content-Length (connection-close delimits body)
    bool no_store_body; // if true, do not allocate body; only count bytes
    bool use_tls;       // opt-in TLS for this session
    bool stream_mode;   // if true, invoke streaming callbacks instead of accumulating body

    // Header processing
    char header_buffer[HTTP_HEADER_MAX];
    size_t header_length;

    // Body processing - dynamic allocation
    char *body_buffer;
    size_t body_buffer_size;
    size_t expected_length;
    size_t total_received;

    // Request data
    char *request_data;
    size_t request_length;

    // Connection management
    struct altcp_pcb *pcb;
    char server_hostname[96]; // optional SNI hostname (for TLS)

    // Callback for completion
    void (*completion_callback)(const char *body, size_t length, bool success, void *arg);
    void *callback_arg;

    // Streaming callbacks (enabled when stream_mode==true)
    void (*stream_on_header)(const char *header, size_t header_len, int status_code,
                             int content_length, void *arg);
    void (*stream_on_data)(const uint8_t *data, size_t len, void *arg);
    void (*stream_on_complete)(bool success, void *arg);
    void *stream_arg;

    // Error tracking
    err_t last_error;
    bool transfer_was_successful;

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

// === Public API ===

// Initialize HTTP client system
bool http_client_init(void);

// Main HTTP request function
http_result_t
http_request_async(const ip_addr_t *server_ip, uint16_t port, const char *request_data,
                   void (*callback)(const char *body, size_t length, bool success, void *arg),
                   void *callback_arg);

// Same as http_request_async but does not store the response body in RAM; only counts bytes.
http_result_t http_request_async_count_only(
    const ip_addr_t *server_ip, uint16_t port, const char *request_data,
    void (*callback)(const char *body, size_t length, bool success, void *arg), void *callback_arg);

typedef void (*data_callback_fn)(const char *response_data, size_t length, void *arg);

// Synchronous HTTP request: dispatch + poll until completion or timeout.
// cb is invoked with the response body on success (pass NULL to skip body processing).
// timeout_ms <= 0 uses 5000 ms. Returns true on success.
bool http_request_sync(const ip_addr_t *server_ip, uint16_t port, const char *request,
                       int timeout_ms, data_callback_fn cb, void *cb_arg);

// Same as http_request_sync but suppresses body storage (count-only mode).
bool http_request_sync_count_only(const ip_addr_t *server_ip, uint16_t port, const char *request,
                                  int timeout_ms);

// Streaming request: does not store body; calls header/data/complete callbacks as data arrives.
http_result_t
http_request_async_stream(const ip_addr_t *server_ip, uint16_t port, const char *request_data,
                          void (*on_header)(const char *header, size_t header_len, int status_code,
                                            int content_length, void *arg),
                          void (*on_data)(const uint8_t *data, size_t len, void *arg),
                          void (*on_complete)(bool success, void *arg), void *cb_arg);

// Session management
bool http_session_is_active(void);

// --- Callback system for use-case data processing ---

// Set callback for direct invocation via http_invoke_data_callback().
// Used by Homematic's async orchestration. For sync requests, pass cb to http_request_sync().
void set_data_callback(data_callback_fn callback, void *arg);

// Invoke the registered data callback from use-case request chains (e.g. Homematic sequential)
void http_invoke_data_callback(const char *data, size_t length, void *arg);

// Returns the server time (UTC) extracted from the last HTTP Date header.
// Returns true if a valid Date header was parsed, false otherwise.
bool http_get_server_time(rtc_time_t *out);

// Returns the HTTP status code from the last response (e.g. 200, 401, 500).
// Returns 0 if no HTTP response was received (pure TCP failure or not yet requested).
int http_get_last_status_code(void);

#endif // HTTP_CLIENT_H
