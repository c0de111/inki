#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "lwip/tcp.h"
#include "pico/types.h"
#include <stdbool.h>

#define MAX_FORM_FIELDS 128
#define MAX_FIELD_LENGTH 128
#define USER_INTERACTION_TIMEOUT_MS                                                                \
    (5 * 60 * 1000) // reset active time for user interaction: 5 minutes

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Formularfelder
    char roomname[16];
    int type;
    int epapertype;
    int refresh_minutes_by_pushbutton[8];
    int number_of_seats;
    int wifi_reconnect_minutes;
    int watchdog_time;
    int number_wifi_attempts;
    int wifi_timeout;
    int max_wait_data_wifi;
    bool show_query_date;
    bool query_only_at_officehours;
    float conversion_factor;
    bool telemetry_enabled;
    int telemetry_timeout_ms;
    char telemetry_host[32];
    int telemetry_port;
    char telemetry_token[MAX_FIELD_LENGTH];
    char telemetry_label[32];
    char wifi_ssid[64];
    char wifi_password[64];

    // Optional bestehende Felder
    char text[128][MAX_FIELD_LENGTH];
    bool aborted;
    int hour, minute, second;
    int day, date, month, year;
    int unit_ms; // generic millisecond value for forms like Morse
    char action[16];
    char align[8];
    char font_size[16];
    // Homematic (ephemeral toggles)
    bool homematic_service_messages;
} web_submission_t;

typedef enum {
    UPLOAD_NONE,
    UPLOAD_LOGO,
    UPLOAD_FIRMWARE,
    UPLOAD_FORM_WIFI,
#ifdef USE_CASE_SEATSURFING
    UPLOAD_FORM_SEATSURFING,
#elif defined(USE_CASE_HISTORIAN)
    UPLOAD_FORM_HISTORIAN,
#elif defined(USE_CASE_HOMEMATIC)
    UPLOAD_FORM_HOMEMATIC,
#elif defined(USE_CASE_WEATHERMAP)
    UPLOAD_FORM_WEATHERMAP,
#endif
    UPLOAD_FORM_DEVICE,
    UPLOAD_FORM_CLOCK,
    UPLOAD_FORM_SETTINGS_IMPORT,
    UPLOAD_FORM_MESSAGE
} upload_type_t;

typedef struct {
    upload_type_t type;
    size_t total_received;
    size_t expected_length;
    bool active;

    bool header_complete;
    size_t header_length;
    char header_buffer[4096];

    uint32_t flash_offset;
    char form_buffer[4096];

    // int upload_percent; // global or in upload_session
    int flash_estimated_duration;
} upload_session_t;

extern upload_session_t upload_session;

typedef void (*submission_handler_t)(const web_submission_t *data);

// Starts the setup webserver with handler for /wifi
void start_setup_webserver();

// Passes the global shutdown time to the webserver (for remaining-runtime display)
void webserver_set_shutdown_time(absolute_time_t t);

// Utility functions for HTML page generation
void add_timeout_info(char *buf, size_t buf_size);
void send_response(struct tcp_pcb *tpcb, const char *body);
void send_response_with_content_type_and_disposition(struct tcp_pcb *tpcb, const char *body,
                                                     const char *content_type,
                                                     const char *content_disposition);
bool webserver_upload_in_progress(void);
bool webserver_firmware_upload_active(void);

#ifdef __cplusplus
}
#endif

#endif // WEBSERVER_H
