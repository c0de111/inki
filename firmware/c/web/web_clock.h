#ifndef WEB_CLOCK_H
#define WEB_CLOCK_H

#include <stddef.h>

void build_clock_page(char *buf, size_t sz, const char *message);
void handle_form_clock(const char *body, size_t len, char *buf, size_t sz);

#endif // WEB_CLOCK_H
