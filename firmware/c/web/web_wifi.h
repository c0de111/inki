#ifndef WEB_WIFI_H
#define WEB_WIFI_H

#include <stddef.h>

void build_wifi_config_page(char *buf, size_t sz, const char *message);
void handle_form_wifi(const char *body, size_t len, char *buf, size_t sz);

#endif // WEB_WIFI_H
