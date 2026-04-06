#ifndef SEATSURFING_CLIENT_H
#define SEATSURFING_CLIENT_H

#include "seatsurfing/config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool is_available;
    char user_email[64]; // empty if available
    char desk_name[32];  // "Desk 3", "Platz 1", etc.
} seat_info_t;

extern seat_info_t seatsurfing_data[SEATSURFING_MAX_SEATS];

void ss_format_seat(const seat_info_t *seat, char *out, size_t out_len, size_t max_chars);

#endif // SEATSURFING_CLIENT_H
