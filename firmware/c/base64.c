/**
 * @file base64.c
 * @brief Base64 encoding and HTTP Basic Authentication utilities
 */

#include "base64.h"
#include <stdio.h>
#include <string.h>

/** Base64 encoding table */
static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief Encode binary data to Base64
 * @param data Input data to encode
 * @param input_length Length of input data
 * @param output Output buffer for Base64 string
 * @param output_size Size of output buffer
 */
void base64_encode(const void *data, size_t input_length, char *output, size_t output_size) {
    const uint8_t *input = (const uint8_t *)data;
    size_t i = 0, j = 0;

    // Process input in 3-byte chunks
    while (i < input_length && (j + 4) < output_size) {
        size_t remain = input_length - i;
        uint32_t octet_a = input[i++];
        uint32_t octet_b = (remain > 1) ? input[i++] : 0;
        uint32_t octet_c = (remain > 2) ? input[i++] : 0;

        // Pack 3 bytes into 24-bit value
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        // Extract 6-bit groups and map to Base64 characters
        output[j++] = base64_table[(triple >> 18) & 0x3F];
        output[j++] = base64_table[(triple >> 12) & 0x3F];
        output[j++] = (remain > 1) ? base64_table[(triple >> 6) & 0x3F] : '=';
        output[j++] = (remain > 2) ? base64_table[triple & 0x3F] : '=';
    }
    output[j] = '\0';
}
