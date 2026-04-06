#ifndef BOOT_INPUT_H
#define BOOT_INPUT_H

#include "st25_io.h"
#include <stdbool.h>

// Boot input: unified result from all boot-time input sources
// (pushbuttons, NFC, RTC alarm). The source field uses the INPUT_BUTTON()
// and INPUT_NFC() namespaces defined in use_case.h.
typedef struct {
    int source;        // INPUT_BUTTON(n) or INPUT_NFC(op)
    bool reject;       // spurious NFC wake — caller should shut down
    bool st25_present; // ST25 chip detected (for cleanup at shutdown)
} boot_input_t;

// Read all boot-time input sources and return unified result.
// Call after I2C bus is initialized.
boot_input_t read_boot_input(void);

// Module state — read by telemetry (http_client.c) and NFC text renderer
extern const char *wake_source;
extern char nfc_text_buf[];

#endif // BOOT_INPUT_H
