#ifndef WEBSERVER_UTILS_H
#define WEBSERVER_UTILS_H

#include "lwip/pbuf.h"
#include "webserver.h"
#include <stddef.h>
#include <stdint.h>

uint32_t crc32_calculate(const uint8_t *data, size_t length);

void safe_flash_copy(void *dest, const void *flash_src, size_t len);

void url_decode(char *dst, const char *src, size_t dst_len);

void parse_form_fields(const char *body, size_t len, web_submission_t *result);

size_t copy_pbuf_chain(const struct pbuf *p, uint8_t *dest, size_t max_len);

void reset_upload_session(void);

#endif // WEBSERVER_UTILS_H
