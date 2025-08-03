#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>
#include <stdint.h>

void base64_encode(const void *data, size_t input_length, char *output, size_t output_size);

#endif
