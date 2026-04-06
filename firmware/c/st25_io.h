#ifndef ST25_IO_H
#define ST25_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ST25 wake request: 16-byte INKI payload from NFC EEPROM */
typedef struct {
    uint8_t magic[4];      /* "INKI" */
    uint8_t version;       /* must be 1 */
    uint8_t opcode;        /* command opcode */
    uint16_t duration_min; /* little-endian, 1-1440 */
    uint32_t unix_seconds; /* little-endian */
    uint32_t nonce;        /* little-endian */
} st25_inki_request_t;

/* Opcode definitions — opcodes act as "soft pushbuttons" for page selection */
#define ST25_OPCODE_REFRESH 0x01u /* immediate refresh (page 0) */
#define ST25_OPCODE_PAGE_0 0x11u  /* page 0: default / seatsurfing */
#define ST25_OPCODE_PAGE_2 0x12u  /* page 2: universal decision maker */
#define ST25_OPCODE_TEXT 0x20u    /* display text message from NFC EEPROM */

/* Last-processed nonce (4 bytes) — for stale request deduplication */
#define ST25_NONCE_ADDR 0x0004u

/* Text payload area in ST25 user EEPROM (before the request slot) */
#define ST25_TEXT_ADDR 0x0008u
#define ST25_TEXT_MAX_LEN 232u

/* Result of ST25 boot check */
typedef struct {
    bool present;            /* ST25 detected on I2C */
    bool request_valid;      /* valid INKI request found */
    bool rf_field_on;        /* RF field currently active */
    uint8_t it_sts;          /* IT_STS_Dyn: latched interrupt flags (0=no RF activity) */
    st25_inki_request_t req; /* decoded request (if valid) */
} st25_boot_result_t;

/*
 * Full ST25 boot sequence: power on, init driver, configure GPO,
 * read request, check RF field. Call after I2C bus is initialized.
 * Returns result with presence/request/field state.
 */
st25_boot_result_t st25_boot_check(void);

/*
 * Read text payload from ST25 EEPROM (text area before request slot).
 * Returns number of bytes read (0 on error). Output is null-terminated.
 */
int st25_read_text(char *buf, size_t buf_size);

/*
 * Clear the request slot in ST25 EEPROM (write zeros).
 */
bool st25_clear_request(void);

/*
 * Save the nonce of a processed request to EEPROM (dedup marker).
 * Call after successfully processing an NFC request.
 */
bool st25_save_processed_nonce(uint32_t nonce);

/*
 * Power off ST25 VCC (set GP18 low). Call before power-down.
 */
void st25_power_off(void);

#endif /* ST25_IO_H */
