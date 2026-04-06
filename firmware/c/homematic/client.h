#ifndef HOMEMATIC_CLIENT_H
#define HOMEMATIC_CLIENT_H

#include "homematic/config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// --- Value types and parsed data ---

typedef enum { HM_TYPE_NONE, HM_TYPE_DOUBLE, HM_TYPE_I4, HM_TYPE_BOOL, HM_TYPE_STRING } hm_type_t;

typedef struct {
    bool valid;
    bool fault;
    hm_type_t type;
    double dval;
    int ival;
    bool bval;
    char sval[32];
    char unit[8];
} hm_item_value_t;

#define HM_MAX_SERVICE_MSGS 5

extern hm_item_value_t homematic_values[HOMEMATIC_MAX_ITEMS];
extern char homematic_service_msgs[HM_MAX_SERVICE_MSGS][80];
extern char homematic_service_addr[HM_MAX_SERVICE_MSGS][24];
extern int homematic_service_count;

// Data interpretation helpers (used by epaper_pages)
const char *derive_unit_for_key(const char *key);
void summarize_addr(const char *full, char *out, size_t n);

// --- XML-RPC request builders (used by epaper_pages) ---

int homematic_build_multicall(char *buf, size_t n, const homematic_config_t *cfg);

#endif // HOMEMATIC_CLIENT_H
