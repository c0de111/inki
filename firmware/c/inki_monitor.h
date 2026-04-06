#ifndef INKI_MONITOR_H
#define INKI_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float battery_before_wifi_v;
    float battery_after_wifi_v;
    float coin_cell_v;
    float pico_temp_c;
    uint32_t telemetry_send_elapsed_ms;
    bool query_ok;
    const char *label;       // Optional human-readable device label (may be NULL/empty)
    const char *wake_source; // Wake source identifier (e.g. "nfc", "rtc", "button", NULL)
} inki_monitor_sample_t;

// Sends one telemetry sample while Wi-Fi STA is already online.
// Returns false on any error; callers must continue normal firmware flow.
bool inki_monitor_send_sample(const inki_monitor_sample_t *sample);

#endif // INKI_MONITOR_H
