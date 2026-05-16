#ifndef HTTP_PARSE_H
#define HTTP_PARSE_H

#include <stddef.h>

// Returns the Content-Length value, or -1 if the header is absent or unparsable.
int http_parse_content_length(const char *header);

// Returns the HTTP status code (e.g. 200, 404), or -1 if the status line is malformed.
int http_parse_status_code(const char *header);

// Extracts the hostname (without optional :port) from the Host: header of an HTTP
// request. Writes an empty string to out_host if no Host header is found.
void http_parse_host(const char *request, char *out_host, size_t out_size);

#endif // HTTP_PARSE_H
