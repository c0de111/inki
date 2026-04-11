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

// Sends a telemetry sample. battery_before_v is read before wifi_connect; the rest are sampled
// inside.
void inki_monitor_send_telemetry(float battery_before_v, float coin_cell_v, bool query_ok);

#endif // INKI_MONITOR_H
