#ifndef SEATSURFING_CLIENT_H
#define SEATSURFING_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef USE_CASE_SEATSURFING

typedef struct {
    bool is_available;
    char user_email[64];  // empty if available
    char desk_name[32];   // "Desk 3", "Platz 1", etc.
} seat_info_t;

// Build HTTP GET request for SeatSurfing availability API.
// `auth_b64` is the Base64 of "username:password".
int seatsurfing_build_http_request(char* buffer, size_t buffer_size,
                                   const char* host,
                                   const char* location_id,
                                   const char* auth_b64);

// Parse bulk availability JSON; returns info for target id or name (or first entry if NULL)
seat_info_t seatsurfing_parse_seat_info(const char* json, const char* target_space_id_or_name);

#endif // USE_CASE_SEATSURFING

#endif // SEATSURFING_CLIENT_H
