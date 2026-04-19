#ifndef WEB_DEVICE_SETTINGS_H
#define WEB_DEVICE_SETTINGS_H

#include <stddef.h>

void build_device_config_page(char *buf, size_t sz, const char *message);
void handle_form_device_config(const char *body, size_t len, char *buf, size_t sz);

#endif // WEB_DEVICE_SETTINGS_H
