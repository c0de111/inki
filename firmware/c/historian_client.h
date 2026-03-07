#ifndef HISTORIAN_CLIENT_H
#define HISTORIAN_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef USE_CASE_HISTORIAN

// Build HTTP JSON-RPC request for CCU-Historian
int historian_build_http_request(char *buffer, size_t buffer_size, const char *host,
                                 int datapoint_id, uint64_t start_time_ms, uint64_t end_time_ms);

#endif // USE_CASE_HISTORIAN

#endif // HISTORIAN_CLIENT_H
