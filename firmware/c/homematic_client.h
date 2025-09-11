#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "homematic_config.h"

// Build a single getValue XML-RPC body
int homematic_build_getvalue(char* buf, size_t n, const char* address, const char* key);

// Build a system.multicall XML-RPC body for all configured items
int homematic_build_multicall(char* buf, size_t n, const homematic_config_t* cfg);

// Build HTTP/1.0 POST with Content-Length for given XML body
int homematic_build_http_post(char* out, size_t n, const char* host, const char* xml, int xml_len);

// Build getParamsetDescription (VALUES) XML-RPC body to query metadata incl. UNIT
int homematic_build_getparamsetdesc(char* buf, size_t n, const char* address);
