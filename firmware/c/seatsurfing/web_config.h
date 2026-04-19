#pragma once

#include <stddef.h>

void ss_render_config_page(char *buf, size_t sz, const char *msg);
const char *ss_handle_config_form(const char *body, size_t len);
