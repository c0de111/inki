#pragma once
#include <stdint.h>

#define SEATSURFING_HOST_MAX_LEN     64
#define SEATSURFING_USER_MAX_LEN     64
#define SEATSURFING_PWD_MAX_LEN      64
#define SEATSURFING_SPACE_ID_LEN     64
#define SEATSURFING_LOCATION_ID_LEN  64

typedef struct {
    char host[SEATSURFING_HOST_MAX_LEN];
    uint8_t ip[4];
    uint16_t port;
    char username[SEATSURFING_USER_MAX_LEN];
    char password[SEATSURFING_PWD_MAX_LEN];
    char space_id[SEATSURFING_SPACE_ID_LEN];
    char location_id[SEATSURFING_LOCATION_ID_LEN];
} seatsurfing_config_data_t;

typedef struct {
    seatsurfing_config_data_t data;
    uint32_t crc32;
} seatsurfing_config_t;
