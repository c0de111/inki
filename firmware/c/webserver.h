#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <stdbool.h>
#include "pico/types.h"

#define MAX_FORM_FIELDS 128
#define MAX_FIELD_LENGTH 128

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

        // Optional bestehende Felder
        char text[128][MAX_FIELD_LENGTH];
        bool aborted;
        int hour, minute, second;
        int day, date, month, year;
    } web_submission_t;

    typedef enum {
        UPLOAD_NONE,
        UPLOAD_LOGO,
        UPLOAD_FIRMWARE,
        UPLOAD_FORM_WIFI,
        UPLOAD_FORM_SEATSURFING,
        UPLOAD_FORM_DEVICE,
        UPLOAD_FORM_CLOCK
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

        // int upload_percent; // global oder in upload_session
        int flash_estimated_duration;
    } upload_session_t;


    static upload_session_t upload_session;

    typedef void (*submission_handler_t)(const web_submission_t *data);

    // Startet den Setup-Webserver mit Handler für /wifi
    void start_setup_webserver();

    // Übergibt dem Webserver die globale Shutdown-Zeit (für Restlaufzeitanzeige)
    void webserver_set_shutdown_time(absolute_time_t t);

    #ifdef __cplusplus
}
#endif

#endif // WEBSERVER_H
