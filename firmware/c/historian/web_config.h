#pragma once

#include <stddef.h>

void hist_render_config_page(char *buf, size_t sz, const char *msg);
const char *hist_handle_config_form(const char *body, size_t len);
