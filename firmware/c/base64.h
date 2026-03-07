/**
 * @file base64.h
 * @brief Base64 encoding and HTTP Basic Authentication utilities
 */

#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Encode binary data to Base64
 * @param data Input data to encode
 * @param input_length Length of input data
 * @param output Output buffer for Base64 string
 * @param output_size Size of output buffer
 */
void base64_encode(const void *data, size_t input_length, char *output, size_t output_size);

#endif
