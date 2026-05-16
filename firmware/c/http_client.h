#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "lwip/ip_addr.h"
#include "rtc.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Buffer sizes used by callers when stack-allocating request/body buffers.
#ifndef HTTP_REQUEST_MAX
#define HTTP_REQUEST_MAX 2048
#endif

#ifndef HTTP_JSON_BODY_MAX
#define HTTP_JSON_BODY_MAX 512
#endif

typedef enum {
    HTTP_SUCCESS = 0,
    HTTP_ERROR_CONNECTION,
    HTTP_ERROR_MEMORY,
    HTTP_ERROR_TIMEOUT,
    HTTP_ERROR_INVALID_RESPONSE,
    HTTP_ERROR_BUFFER_OVERFLOW,
} http_result_t;

typedef void (*data_callback_fn)(const char *response_data, size_t length, void *arg);

bool http_client_init(void);

http_result_t
http_request_async(const ip_addr_t *server_ip, uint16_t port, const char *request_data,
                   void (*callback)(const char *body, size_t length, bool success, void *arg),
                   void *callback_arg);

// Identical to http_request_async but does not buffer the body — only counts bytes.
http_result_t http_request_async_count_only(
    const ip_addr_t *server_ip, uint16_t port, const char *request_data,
    void (*callback)(const char *body, size_t length, bool success, void *arg), void *callback_arg);

// Dispatch + poll until completion or timeout. cb is invoked with the response body on success.
// Pass NULL cb to skip body processing. timeout_ms <= 0 falls back to 5000 ms.
bool http_request_sync(const ip_addr_t *server_ip, uint16_t port, const char *request,
                       int timeout_ms, data_callback_fn cb, void *cb_arg);

bool http_request_sync_count_only(const ip_addr_t *server_ip, uint16_t port, const char *request,
                                  int timeout_ms);

// Streaming: no body buffer; header/data/complete callbacks fire as bytes arrive.
http_result_t
http_request_async_stream(const ip_addr_t *server_ip, uint16_t port, const char *request_data,
                          void (*on_header)(const char *header, size_t header_len, int status_code,
                                            int content_length, void *arg),
                          void (*on_data)(const uint8_t *data, size_t len, void *arg),
                          void (*on_complete)(bool success, void *arg), void *cb_arg);

bool http_session_is_active(void);

// Server time (UTC) from the last response's Date header. False if unavailable.
bool http_get_server_time(rtc_time_t *out);

// HTTP status code from the last response (e.g. 200, 401). 0 if no response yet.
int http_get_last_status_code(void);

#endif // HTTP_CLIENT_H
