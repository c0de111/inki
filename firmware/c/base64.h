#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>
#include <stdint.h>

void base64_encode(const void *data, size_t input_length, char *output, size_t output_size);

// HTTP Basic Authentication helper
void create_basic_auth_header(const char *username, const char *password, char *output_base64);

#endif
