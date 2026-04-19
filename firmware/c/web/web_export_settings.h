#ifndef WEB_EXPORT_SETTINGS_H
#define WEB_EXPORT_SETTINGS_H

#include <stddef.h>

void build_settings_transfer_page(char *buf, size_t sz, const char *message);
void build_settings_export_txt(char *buf, size_t sz, char *disposition, size_t disp_sz);
void handle_form_settings_import(const char *body, size_t len, char *buf, size_t sz);

#endif // WEB_EXPORT_SETTINGS_H
