#include "base64.h"
#include <stdio.h>
#include <string.h>

static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void base64_encode(const void *data, size_t input_length, char *output, size_t output_size) {
    const uint8_t *input = (const uint8_t *)data;
    size_t i = 0, j = 0;
    while (i < input_length && (j + 4) < output_size) {
        uint32_t octet_a = i < input_length ? input[i++] : 0;
        uint32_t octet_b = i < input_length ? input[i++] : 0;
        uint32_t octet_c = i < input_length ? input[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        output[j++] = base64_table[(triple >> 18) & 0x3F];
        output[j++] = base64_table[(triple >> 12) & 0x3F];
        output[j++] = (i > input_length + 1) ? '=' : base64_table[(triple >> 6) & 0x3F];
        output[j++] = (i > input_length)     ? '=' : base64_table[triple & 0x3F];
    }
    output[j] = '\0';
}

void create_basic_auth_header(const char *username, const char *password, char *output_base64) {
    char userpass[128];
    snprintf(userpass, sizeof(userpass), "%s:%s", username, password);
    // Use base64 module with buffer protection (max output: 192 bytes for 128 input)
    base64_encode(userpass, strlen(userpass), output_base64, 192);
}
