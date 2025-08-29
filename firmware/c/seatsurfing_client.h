#ifndef SEATSURFING_CLIENT_H
#define SEATSURFING_CLIENT_H

#include <stddef.h>

#ifdef USE_CASE_SEATSURFING

// Build HTTP GET request for SeatSurfing availability API.
// `auth_b64` is the Base64 of "username:password".
int seatsurfing_build_http_request(char* buffer, size_t buffer_size,
                                   const char* host,
                                   const char* location_id,
                                   const char* space_id,
                                   const char* auth_b64);

#endif // USE_CASE_SEATSURFING

#endif // SEATSURFING_CLIENT_H

