#ifndef WEBSERVER_UTILS_H
#define WEBSERVER_UTILS_H

#include "webserver.h"
#include <stddef.h>
#include <stdint.h>

void url_decode(char *dst, const char *src, size_t dst_len);

void parse_form_fields(const char *body, size_t len, web_submission_t *result);

void reset_upload_session(void);

#endif // WEBSERVER_UTILS_H
