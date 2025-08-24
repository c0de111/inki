/*
 * ==============================================================================
 * HTTP Processing Overview – inki Webserver
 * ==============================================================================
 *
 * Connection ──► recv_cb()
 *              │
 *              ├─ Collect HTTP header into upload_session.header_buffer
 *              │
 *              ├─ Once \r\n\r\n is found:
 *              │    └─ header_complete = true
 *              │
 *              └─ Analyze first request line:
 *                   - GET /...    → send_..._page(tpcb)
 *                   - POST /...   → handle_post_...(tpcb, ...)
 *                                  │
 *                                  ├─ Form uploads:
 *                                  │    ├─ Copy body into form_buffer (RAM)
 *                                  │    └─ Call handle_form_...(tpcb, buffer)
 *                                  │
 *                                  └─ Binary uploads (logo / firmware):
 *                                       ├─ Erase flash sector
 *                                       ├─ Write body chunks:
 *                                       │     - Fill flash_writer.buffer
 *                                       │     - On 4096 bytes: flush_page_to_flash()
 *                                       └─ On completion:
 *                                             - Flush last page
 *                                             - Send "Upload OK" page
 *
 * Responses:
 *  send_response(tpcb, body) → send_next_chunk() → tcp_write() in 1024-byte chunks
 *
 * Data structures:
 *  - upload_session_t
 *      .header_buffer[2048], .form_buffer[32k]
 *      .active, .header_complete, .expected_length, .total_received, .type
 *
 *  - flash_writer
 *      .buffer[4096], .buffer_filled, .flash_offset
 *
 * GET routes:
 *  /                 → Landing page
 *  /wifi             → Wi-Fi config
 *  /seatsurfing      → Seatsurfing API settings
 *  /device_config    → Device configuration
 *  /clock            → Set RTC time
 *  /device_status    → System status
 *  /upload_logo      → Logo upload page
 *  /firmware_update  → Firmware update page
 *  /shutdown         → Trigger RTC-based shutdown
 *
 * POST routes:
 *  /wifi             → handle_post_wificopied()
 *  /seatsurfing      → handle_post_seatsurfingcopied()
 *  /device_config    → handle_post_devicecopied()
 *  /clock            → handle_post_clockcopied()
 *  /upload_logo      → handle_post_upload_logo()
 *  /firmware_update  → handle_post_firmware_update()
 *  /delete_logo      → Immediate flash erase
 *
 * Note:
 *  Large uploads (firmware, logo) are streamed directly into flash memory
 *  → no full buffering in RAM required.
 *
 * ==============================================================================
 */

#include "hardware/sync.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "webserver.h"
#include "lwip/tcp.h"
#include <string.h>
#include <stdio.h>
#include "pico/time.h"
#include "debug.h"
#include "main.h"
#include "flash.h"
#include "wifi.h"
#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "ds3231.h"
#include "webserver_utils.h"
#include "webserver_pages.h"

#define USER_INTERACTION_TIMEOUT_MS (5 * 60 * 1000)  // reset active time for user interaction: 5 minutes
#define TCP_CHUNK_SIZE 1024

#include <stdint.h>
#include <stddef.h>



void reset_upload_session(void) {
    upload_session.active = false;
    upload_session.header_complete = false;
    upload_session.header_length = 0;
    upload_session.total_received = 0;
    upload_session.expected_length = 0;
    upload_session.flash_offset = 0;
    upload_session.type = UPLOAD_NONE;
}


// Marks a firmware slot as valid by setting valid_flag = 1
// in its 256-byte firmware_header_t structure at the start of the slot.
// The header resides at offset 0x000 within a 4 KB flash sector.
// The sector must be erased and rewritten as a whole.
void mark_firmware_valid(uint32_t flash_offset) {
    // 1. Read the 4 KB flash sector that contains the firmware header
    uint8_t sector_buffer[FLASH_SECTOR_SIZE];
    memcpy(sector_buffer, FLASH_PTR(flash_offset), FLASH_SECTOR_SIZE);

    // 2. Patch the valid_flag inside the firmware_header_t
    firmware_header_t* header = (firmware_header_t*)sector_buffer;

    debug_log("Firmware header before setting valid_flag:\n");
    debug_log("  magic         : '%.*s'\n", (int)sizeof(header->magic), header->magic);
    debug_log("  valid_flag    : %u\n", header->valid_flag);
    debug_log("  build_date    : '%.*s'\n", (int)sizeof(header->build_date), header->build_date);
    debug_log("  git_version   : '%.*s'\n", (int)sizeof(header->git_version), header->git_version);
    debug_log("  firmware_size : %u\n", header->firmware_size);
    debug_log("  slot          : %u\n", header->slot);
    debug_log("  crc32         : 0x%08X\n", header->crc32);

    header->valid_flag = 1;

    // 3. Erase and reprogram the 4 KB flash sector
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);
    flash_range_program(flash_offset, sector_buffer, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);

    debug_log("Firmware marked valid (sector-based rewrite).\n");
}

static absolute_time_t shutdown_time = {0};
extern const unsigned char gImage_eSign_100x100_3[];

// Shutdown-Callback
static int64_t shutdown_callback(alarm_id_t id, void *user_data) {
    extern ds3231_t ds3231;
    debug_log("Shutdown-Callback wurde aufgerufen\n");
    set_alarmclock_and_powerdown(&ds3231);
    // watchdog_reboot(0, 0, 0);
    return 0;
}

typedef struct {
    bool active;
    size_t total_received;
    size_t expected_length;
    uint8_t buffer[32768];  // Max. Logo-Größe
} upload_state_t;

typedef struct {
    struct tcp_pcb* pcb;
    const char* ptr;
    int remaining;
    int chunk_index;
    int body_len;
    char* body_copy;
} response_state_t;

static upload_state_t logo_upload = {0};

// Wandelt URL-kodierten String src in dst um (z. B. %20 → Leerzeichen, + → Leerzeichen)

void webserver_set_shutdown_time(absolute_time_t t) {
    shutdown_time = t;
}
void add_timeout_info(char *buf, size_t buf_size) {
    int64_t now_us = to_us_since_boot(get_absolute_time());
    int64_t target_us = to_us_since_boot(shutdown_time);
    int64_t diff_us = target_us - now_us;

    if (diff_us > 0) {
        int seconds = diff_us / 1000000;
        int minutes = seconds / 60;
        seconds %= 60;
        snprintf(buf, buf_size,
                 "<small>Remaining time before shutdown: %d:%02d minutes</small>",
                 minutes, seconds);
    } else {
        snprintf(buf, buf_size,
                 "<small>Setup period expired</small>");
    }
}


static void parse_form_fields(const char *body, int len, web_submission_t *result) {
    memset(result, 0, sizeof(web_submission_t));

    const char *ptr = body;
    while (ptr < body + len) {
        const char *eq = strchr(ptr, '=');
        if (!eq || eq >= body + len) break;

        const char *key = ptr;
        const char *val = eq + 1;

        // Fehler behoben: Suche nach '&' nur im erlaubten Bereich
        const char *amp = memchr(val, '&', body + len - val);
        if (!amp) amp = body + len;

        int key_len = eq - key;
        int val_len = amp - val;

        // Hilfsbuffer zur temporären Entschlüsselung
        char value_buf[MAX_FIELD_LENGTH] = {0};
        int j = 0;
        for (int i = 0; i < val_len && j < (int)sizeof(value_buf) - 1; i++) {
            if (val[i] == '+') {
                value_buf[j++] = ' ';
            } else if (val[i] == '%' && i + 2 < val_len) {
                char hex[3] = {val[i+1], val[i+2], 0};
                value_buf[j++] = (char)strtol(hex, NULL, 16);
                i += 2;
            } else {
                value_buf[j++] = val[i];
            }
        }
        value_buf[j] = 0;

        // Allgemeine Textfelder (optional)
        if (key_len >= 5 && strncmp(key, "text", 4) == 0) {
            int idx = atoi(&key[4]) - 1;
            if (idx >= 0 && idx < 128) {
                strncpy(result->text[idx], value_buf, MAX_FIELD_LENGTH - 1);
                result->text[idx][MAX_FIELD_LENGTH - 1] = 0;
            }
        }

        // Abbruchfeld
        else if (key_len == 5 && strncmp(key, "abort", 5) == 0) {
            result->aborted = true;
        }

        // Zeit- und Datumsfelder
        else if (key_len == 4 && strncmp(key, "hour", 4) == 0) result->hour = atoi(value_buf);
        else if (key_len == 6 && strncmp(key, "minute", 6) == 0) result->minute = atoi(value_buf);
        else if (key_len == 6 && strncmp(key, "second", 6) == 0) result->second = atoi(value_buf);
        else if (key_len == 3 && strncmp(key, "day", 3) == 0) result->day = atoi(value_buf);
        else if (key_len == 4 && strncmp(key, "date", 4) == 0) result->date = atoi(value_buf);
        else if (key_len == 5 && strncmp(key, "month", 5) == 0) result->month = atoi(value_buf);
        else if (key_len == 4 && strncmp(key, "year", 4) == 0) result->year = atoi(value_buf);

        // Gerätekonfiguration
        else if (key_len == 8 && strncmp(key, "roomname", 8) == 0) {
            strncpy(result->roomname, value_buf, sizeof(result->roomname) - 1);
        }
        else if (key_len == 4 && strncmp(key, "type", 4) == 0) {
            result->type = atoi(value_buf);
        }
        else if (key_len == 10 && strncmp(key, "epapertype", 10) == 0) {
            result->epapertype = atoi(value_buf);
        }
        else if (key_len >= 7 && strncmp(key, "refresh", 7) == 0 && key[7] >= '0' && key[7] <= '7') {
            int idx = key[7] - '0';
            result->refresh_minutes_by_pushbutton[idx] = atoi(value_buf);
        }
        else if (key_len == 15 && strncmp(key, "number_of_seats", 15) == 0) {
            result->number_of_seats = atoi(value_buf);
        }
        else if (key_len == 15 && strncmp(key, "show_query_date", 15) == 0) {
            result->show_query_date = true;
        }
        else if (key_len == 25 && strncmp(key, "query_only_at_officehours", 25) == 0) {
            result->query_only_at_officehours = true;
        }
        else if (key_len == 22 && strncmp(key, "wifi_reconnect_minutes", 22) == 0) {
            result->wifi_reconnect_minutes = atoi(value_buf);
        }
        else if (key_len == 13 && strncmp(key, "watchdog_time", 13) == 0) {
            result->watchdog_time = atoi(value_buf);
        }
        else if (key_len == 20 && strncmp(key, "number_wifi_attempts", 20) == 0) {
            result->number_wifi_attempts = atoi(value_buf);
        }
        else if (key_len == 12 && strncmp(key, "wifi_timeout", 12) == 0) {
            result->wifi_timeout = atoi(value_buf);
        }
        else if (key_len == 18 && strncmp(key, "max_wait_data_wifi", 18) == 0) {
            result->max_wait_data_wifi = atoi(value_buf);
        }
        else if (key_len == 17 && strncmp(key, "conversion_factor", 17) == 0) {
            result->conversion_factor = atof(value_buf);
        }
        ptr = amp + 1;
    }
}

static err_t send_next_chunk(void *arg, struct tcp_pcb *tpcb, u16_t len);
void send_response(struct tcp_pcb* tpcb, const char* body) {
    int body_len = strlen(body);
    // debug_log("send_response: body_len = %d\n", body_len);

    // HTML-Body kopieren, damit er gültig bleibt
    char* body_copy = malloc(body_len);
    if (!body_copy) {
        // debug_log("send_response: malloc failed for body\n");
        return;
    }
    memcpy(body_copy, body, body_len);

    // HTTP-Header erzeugen
    char header[256];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.0 200 OK\r\n"
                              "Content-Type: text/html; charset=UTF-8\r\n"
                              "Content-Length: %d\r\n"
                              "Connection: close\r\n\r\n",
                              body_len);

    // debug_log("send_response: sending header (%d bytes)\n", header_len);
    err_t err = tcp_write(tpcb, header, header_len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        debug_log("send_response: tcp_write(header) failed with code %d\n", err);
        free(body_copy);
        return;
    }

    // Zustand für das Senden vorbereiten
    response_state_t* state = malloc(sizeof(response_state_t));
    if (!state) {
        debug_log("send_response: malloc failed for state\n");
        free(body_copy);
        return;
    }

    state->pcb = tpcb;
    state->ptr = body_copy;
    state->remaining = body_len;
    state->chunk_index = 0;
    state->body_len = body_len;
    state->body_copy = body_copy;

    // Callback setzen und ersten Chunk senden
    tcp_arg(tpcb, state);
    tcp_sent(tpcb, send_next_chunk);
    send_next_chunk(state, tpcb, 0);
}

static err_t send_next_chunk(void* arg, struct tcp_pcb* tpcb, u16_t len) {
    response_state_t* state = (response_state_t*)arg;

    if (state->remaining <= 0) {
        debug_log("send_next_chunk: transfer completed.\n");
        free(state->body_copy);
        free(state);
        return ERR_OK;
    }

    u16_t chunk = state->remaining > TCP_CHUNK_SIZE ? TCP_CHUNK_SIZE : state->remaining;
    err_t err = tcp_write(tpcb, state->ptr, chunk, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        debug_log("send_next_chunk: tcp_write chunk %d failed at %d bytes remaining: err=%d\n",
                  state->chunk_index, state->remaining, err);
        free(state->body_copy);
        free(state);
        return err;
    }

    debug_log("send_next_chunk: chunk %d (%d bytes) written, %d remaining\n",
              state->chunk_index, chunk, state->remaining - chunk);

    state->ptr += chunk;
    state->remaining -= chunk;
    state->chunk_index++;

    tcp_output(tpcb);
    return ERR_OK;
}

static int copy_pbuf_chain(const struct pbuf *p, uint8_t *dest, size_t max_len) {
    size_t copied = 0;
    while (p && copied < max_len) {
        size_t to_copy = p->len;
        if (copied + to_copy > max_len) {
            to_copy = max_len - copied;
        }
        memcpy(dest + copied, p->payload, to_copy);
        copied += to_copy;
        p = p->next;
    }
    return copied;
}

void flush_page_to_flash() {
    if (flash_writer.buffer_filled == 0) {
        debug_log("FLASH: flush_page_to_flash() called, but buffer is empty – skipping\n");
        return;
    }
    watchdog_update();

    // Padding bei Bedarf
    if (flash_writer.buffer_filled % FLASH_PAGE_SIZE != 0) {
        size_t pad_size = FLASH_PAGE_SIZE - flash_writer.buffer_filled;
        memset(flash_writer.buffer + flash_writer.buffer_filled, 0xFF, pad_size);
        debug_log("FLASH: padding %u bytes with 0xFF\n", (unsigned)pad_size);
    }

    // debug_log("FLASH: writing page at offset 0x%X (%u bytes)\n",
              // (unsigned)flash_writer.flash_offset, FLASH_PAGE_SIZE);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(flash_writer.flash_offset,
                        flash_writer.buffer,
                        FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    // debug_log("FLASH: write successful at 0x%X\n", (unsigned)flash_writer.flash_offset);

    flash_writer.flash_offset += FLASH_PAGE_SIZE;
    flash_writer.buffer_filled = 0;
}






void handle_form_wifi(struct tcp_pcb *tpcb, const char *body, size_t len) {
    web_submission_t result = {0};
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    parse_form_fields(body, len, &result);

    wifi_config_t new_cfg = {
        .crc32 = 0
    };
    strncpy(new_cfg.ssid, result.text[0], sizeof(new_cfg.ssid) - 1);
    strncpy(new_cfg.password, result.text[1], sizeof(new_cfg.password) - 1);

    bool ok = save_wifi_config(&new_cfg);

    send_wifi_config_page(tpcb, "✔ WLAN-Daten gespeichert");

    if (ok) {
        debug_log_with_color(COLOR_YELLOW, "SSID & password gespeichert\n");
    } else {
        debug_log_with_color(COLOR_RED, "Fehler beim Speichern\n");
    }
}


void handle_form_seatsurfing(struct tcp_pcb *tpcb, const char *body, size_t len) {
    webserver_set_shutdown_time(make_timeout_time_ms(USER_INTERACTION_TIMEOUT_MS));

    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    web_submission_t result = {0};

    parse_form_fields(body, len, &result);

    seatsurfing_config_t new_cfg = { .crc32 = 0 };

    strncpy(new_cfg.data.host,        result.text[0], sizeof(new_cfg.data.host)        - 1);
    strncpy(new_cfg.data.username,    result.text[1], sizeof(new_cfg.data.username)    - 1);
    strncpy(new_cfg.data.password,    result.text[2], sizeof(new_cfg.data.password)    - 1);

    int ip0, ip1, ip2, ip3;
    if (sscanf(result.text[3], "%d.%d.%d.%d", &ip0, &ip1, &ip2, &ip3) == 4) {
        new_cfg.data.ip[0] = (uint8_t)ip0;
        new_cfg.data.ip[1] = (uint8_t)ip1;
        new_cfg.data.ip[2] = (uint8_t)ip2;
        new_cfg.data.ip[3] = (uint8_t)ip3;
    } else {
        debug_log_with_color(COLOR_RED, "Ungültige IP-Adresse: %s\n", result.text[3]);
    }

    new_cfg.data.port = (uint16_t)atoi(result.text[4]);

    strncpy(new_cfg.data.space_id,    result.text[5], sizeof(new_cfg.data.space_id)    - 1);
    strncpy(new_cfg.data.location_id, result.text[6], sizeof(new_cfg.data.location_id) - 1);

    bool ok = save_seatsurfing_config(&new_cfg);
    if (ok) {
        debug_log_with_color(COLOR_YELLOW, "Seatsurfing-Konfiguration gespeichert.\n");
    } else {
        debug_log_with_color(COLOR_RED, "Fehler beim Speichern der Seatsurfing-Konfiguration.\n");
    }
    send_seatsurfing_config_page(tpcb, "✔ seatsurfing settings stored");
}

static void handle_post_seatsurfingcopied(struct tcp_pcb* tpcb, struct pbuf* p, const char* buffer, int copied) {
    const char* cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD SEATSURFING CONFIG: Content-Length missing\n");
        send_seatsurfing_config_page(tpcb, "Fehlender Content-Length");
        tcp_close(tpcb);
        return;
    }

    upload_session.expected_length = atoi(cl + 15);
    if (upload_session.expected_length >= sizeof(upload_session.form_buffer)) {
        debug_log_with_color(COLOR_RED, "UPLOAD SEATSURFING CONFIG: form body too large\n");
        send_seatsurfing_config_page(tpcb, "Formulardaten zu groß");
        tcp_close(tpcb);
        return;
    }

    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_FORM_SEATSURFING;

    const char* body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);
        memcpy(upload_session.form_buffer, body, body_len);
        upload_session.total_received = body_len;
        tcp_recved(tpcb, copied);

        debug_log("UPLOAD SEATSURFING CONFIG: First POST /seatsurfing body chunk (%d bytes)\n", (int)body_len);

        if (upload_session.total_received >= upload_session.expected_length) {
            upload_session.form_buffer[upload_session.expected_length] = '\0';
            handle_form_seatsurfing(tpcb, upload_session.form_buffer, upload_session.expected_length);
            upload_session.active = false;
            upload_session.header_complete = false;
            upload_session.header_length = 0;
        }
    } else {
        debug_log_with_color(COLOR_RED, "UPLOAD SEATSURFING CONFIG: Header body split error\n");
        send_seatsurfing_config_page(tpcb, "Fehler beim Parsen des Formulars");
        tcp_close(tpcb);
    }
}



void handle_form_device_config(struct tcp_pcb *tpcb, const char *body, size_t len) {
    web_submission_t result = {0};
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    parse_form_fields(body, len, &result);

    device_config_t new_cfg = { .crc32 = 0 }; // wichtig: struct enthält .data + .crc32

    // Bestehende Werte übernehmen
    memcpy(&new_cfg.data, &device_config_flash.data, sizeof(device_config_data_t));

    // Neue Werte eintragen
    strncpy(new_cfg.data.roomname, result.roomname, sizeof(new_cfg.data.roomname) - 1);
    new_cfg.data.type = (RoomType)result.type;
    new_cfg.data.epapertype = (EpaperType)result.epapertype;

    for (int i = 0; i < 8; i++) {
        new_cfg.data.refresh_minutes_by_pushbutton[i] = result.refresh_minutes_by_pushbutton[i];
    }

    new_cfg.data.number_of_seats = result.number_of_seats;
    new_cfg.data.show_query_date = result.show_query_date;
    new_cfg.data.query_only_at_officehours = result.query_only_at_officehours;
    new_cfg.data.wifi_reconnect_minutes = result.wifi_reconnect_minutes;
    new_cfg.data.watchdog_time = result.watchdog_time;
    new_cfg.data.number_wifi_attempts = result.number_wifi_attempts;
    new_cfg.data.wifi_timeout = result.wifi_timeout;
    new_cfg.data.max_wait_data_wifi = result.max_wait_data_wifi;
    new_cfg.data.conversion_factor = result.conversion_factor;

    bool ok = save_device_config(&new_cfg);

    send_device_config_page(tpcb, ok ? "✔ Geräteeinstellungen gespeichert" : "⚠ Fehler beim Speichern");

    if (ok) {
        debug_log_with_color(COLOR_GREEN, "Gerätekonfiguration gespeichert\n");
    } else {
        debug_log_with_color(COLOR_RED, "Fehler beim Speichern der Gerätekonfiguration\n");
    }
}

static void handle_post_devicecopied(struct tcp_pcb* tpcb, struct pbuf* p, const char* buffer, int copied) {
    const char* cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD DEVICE CONFIG: Content-Length missing\n");
        send_device_config_page(tpcb, "Fehlender Content-Length");
        tcp_close(tpcb);
        return;
    }

    upload_session.expected_length = atoi(cl + 15);
    if (upload_session.expected_length >= sizeof(upload_session.form_buffer)) {
        debug_log_with_color(COLOR_RED, "UPLOAD DEVICE CONFIG: form body too large\n");
        send_device_config_page(tpcb, "Formulardaten zu groß");
        tcp_close(tpcb);
        return;
    }

    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_FORM_DEVICE;

    const char* body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);
        memcpy(upload_session.form_buffer, body, body_len);
        upload_session.total_received = body_len;
        tcp_recved(tpcb, copied);

        debug_log("UPLOAD DEVICE CONFIG: First POST /device_config body chunk (%d bytes)\n", (int)body_len);

        if (upload_session.total_received >= upload_session.expected_length) {
            upload_session.form_buffer[upload_session.expected_length] = '\0';
            handle_form_device_config(tpcb, upload_session.form_buffer, upload_session.expected_length);
            upload_session.active = false;
            upload_session.header_complete = false;
            upload_session.header_length = 0;
        }
    } else {
        debug_log_with_color(COLOR_RED, "UPLOAD DEVICE CONFIG: Header body split error\n");
        send_device_config_page(tpcb, "Fehler beim Parsen des Formulars");
        tcp_close(tpcb);
    }
}

static void handle_post_upload_logo(struct tcp_pcb* tpcb, struct pbuf* p, const char* buffer, int copied) {
    const char *cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD LOGO: Content-Length missing\n");
    }

    upload_session.expected_length = atoi(cl + 15);
    debug_log("UPLOAD LOGO: Expected length: %d\n", (int)upload_session.expected_length);

    if (upload_session.expected_length > LOGO_FLASH_SIZE) {
        debug_log_with_color(COLOR_RED, "UPLOAD LOGO: File too large (%d > %d bytes)\n", upload_session.expected_length, LOGO_FLASH_SIZE);
        send_upload_logo_page(tpcb, "too_large");
        upload_session.active = false;
        upload_session.header_complete = false;
        upload_session.header_length = 0;
        tcp_arg(tpcb, NULL);
        tcp_recv(tpcb, NULL);
        tcp_close(tpcb);
    }

    flash_writer.buffer_filled = 0;
    flash_writer.flash_offset = LOGO_FLASH_OFFSET;

    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_LOGO;
    upload_session.flash_offset = LOGO_FLASH_OFFSET;

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(upload_session.flash_offset, LOGO_FLASH_SIZE);
    restore_interrupts(ints);

    debug_log("UPLOAD LOGO: flash erased: %d\n", (int)upload_session.expected_length);

    const char *body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (!body) {
        debug_log_with_color(COLOR_RED, "UPLOAD LOGO: Body not found despite complete header?\n");
    }

    body += 4;
    size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);

    const uint8_t* ptr = (const uint8_t*)body;
    size_t to_copy = body_len;

    while (to_copy > 0) {
        size_t space = FLASH_PAGE_SIZE - flash_writer.buffer_filled;
        size_t chunk = (to_copy < space) ? to_copy : space;

        memcpy(flash_writer.buffer + flash_writer.buffer_filled, ptr, chunk);
        flash_writer.buffer_filled += chunk;
        ptr += chunk;
        to_copy -= chunk;

        if (flash_writer.buffer_filled == FLASH_PAGE_SIZE) {
            flush_page_to_flash();
        }
    }
    upload_session.total_received += body_len;

    debug_log("UPLOAD LOGO: First chunk written (%d bytes)\n", (int)body_len);
    tcp_recved(tpcb, copied);
}

static void handle_post_firmware_update(struct tcp_pcb* tpcb, struct pbuf* p, const char* buffer, int copied) {
    const char *cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD FIRMWARE: Content-Length missing\n");
    }

    upload_session.expected_length = atoi(cl + 15);
    debug_log("UPLOAD FIRMWARE: Expected length: %d\n", (int)upload_session.expected_length);

    if (upload_session.expected_length > FIRMWARE_FLASH_SIZE) {
        debug_log_with_color(COLOR_RED, "UPLOAD FIRMWARE: File too large (%d > %d bytes)\n", upload_session.expected_length, FIRMWARE_FLASH_SIZE);
        send_firmware_update_page(tpcb, "too_large");
        upload_session.active = false;
        upload_session.header_complete = false;
        upload_session.header_length = 0;
        tcp_arg(tpcb, NULL);
        tcp_recv(tpcb, NULL);
        tcp_close(tpcb);
    }
    // Determine target slot based on get_active_firmware_slot_info()
    const char* slot_info = get_active_firmware_slot_info();
    uint32_t target_offset;

    if (strncmp(slot_info, "SLOT_0", 6) == 0) {
        target_offset = FIRMWARE_SLOT1_FLASH_OFFSET;
    } else if (strncmp(slot_info, "SLOT_1", 6) == 0) {
        target_offset = FIRMWARE_SLOT0_FLASH_OFFSET;
    } else {
        // fallback: write to SLOT_0 if active slot is unknown or direct
        target_offset = FIRMWARE_SLOT0_FLASH_OFFSET;
    }

    // Calculate flash erase length (aligned to sector size)
    // size_t erase_length = (upload_session.expected_length + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1); // this is not enough, because of padding to fill the page!

    size_t erase_length = ((upload_session.expected_length + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1)) + FLASH_SECTOR_SIZE;

    // Prepare flash writer
    flash_writer.buffer_filled = 0;
    flash_writer.flash_offset = target_offset;

    // Initialize upload session
    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_FIRMWARE;
    upload_session.flash_offset = target_offset;

    debug_log("UPLOAD FIRMWARE: Writing to offset 0x%08X (active = %.*s)\n",
              target_offset, 6, slot_info);

    int num_sectors = (erase_length + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    int estimated_erase_time_ms = num_sectors * 38;

    int write_blocks = (upload_session.expected_length + 255) / 256;
    int estimated_write_time_ms = write_blocks * 1;

    upload_session.flash_estimated_duration = estimated_erase_time_ms + estimated_write_time_ms;

    debug_log("UPLOAD FIRMWARE: estimate erase=%d ms, write=%d ms → total %d ms\n",
              estimated_erase_time_ms, estimated_write_time_ms, upload_session.flash_estimated_duration);

    if (erase_length > FIRMWARE_FLASH_SIZE) {
        debug_log("ERROR: erase_length (%u) exceeds FIRMWARE_FLASH_SIZE (%u), aborting erase!\n",
                  erase_length, FIRMWARE_FLASH_SIZE);
        upload_session.active = false;
        return ;
    }

    watchdog_update();
    uint32_t ints = save_and_disable_interrupts();
    // flash_range_erase(upload_session.flash_offset, FIRMWARE_FLASH_SIZE); // if erase of full slot is required
    flash_range_erase(upload_session.flash_offset, erase_length);
    restore_interrupts(ints);
    watchdog_update();

    debug_log("UPLOAD FIRMWARE: flash erased: %d\n", (int)upload_session.expected_length);

    const char *body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (!body) {
        debug_log_with_color(COLOR_RED, "UPLOAD FIRMWARE: Body not found despite complete header?\n");
    }

    body += 4;
    size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);

    const uint8_t* ptr = (const uint8_t*)body;
    size_t to_copy = body_len;

    while (to_copy > 0) {
        size_t space = FLASH_PAGE_SIZE - flash_writer.buffer_filled;
        size_t chunk = (to_copy < space) ? to_copy : space;

        memcpy(flash_writer.buffer + flash_writer.buffer_filled, ptr, chunk);
        flash_writer.buffer_filled += chunk;
        ptr += chunk;
        to_copy -= chunk;

        if (flash_writer.buffer_filled == FLASH_PAGE_SIZE) {
            flush_page_to_flash();
        }
    }
    upload_session.total_received += body_len;

    debug_log("UPLOAD FIRMWARE: First chunk written (%d bytes)\n", (int)body_len);
    tcp_recved(tpcb, copied);
}

static void handle_post_wificopied(struct tcp_pcb* tpcb, struct pbuf* p, const char* buffer, int copied) {
    const char *cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD WIFI CONFIG: Content-Length missing\n");
    }

    upload_session.expected_length = atoi(cl + 15);

    if (upload_session.expected_length >= sizeof(upload_session.form_buffer)) {
        debug_log_with_color(COLOR_RED, "UPLOAD WIFI CONFIG: form body too large\n");
        send_wifi_config_page(tpcb, "");  // ggf. mit Fehlerhinweis
        tcp_close(tpcb);
    }

    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_FORM_WIFI;

    const char *body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);
        memcpy(upload_session.form_buffer, body, body_len);
        upload_session.total_received = body_len;
        tcp_recved(tpcb, copied);
        debug_log("UPLOAD WIFI CONFIG: First POST /wifi body chunk (%d bytes)\n", (int)body_len);

        if (upload_session.total_received >= upload_session.expected_length) {
            upload_session.form_buffer[upload_session.expected_length] = '\0';
            handle_form_wifi(tpcb, upload_session.form_buffer, upload_session.expected_length);
            upload_session.active = false;
            upload_session.header_complete = false;
            upload_session.header_length = 0;
        }
    }
}
void handle_form_clock(struct tcp_pcb *tpcb, const char *body, size_t len) {
    const char *line_param = strstr(body, "line=");
    if (!line_param) {
        debug_log_with_color(COLOR_RED, "POST /clock: Kein line= Parameter\n");
        send_clock_page(tpcb, "❌ Kein line= Parameter.");
        return;
    }

    char raw_line[128] = {0};
    char decoded_line[128] = {0};
    sscanf(line_param + 5, "%127[^&\r\n]", raw_line);  // robust
    url_decode(decoded_line, raw_line, sizeof(decoded_line));

    debug_log("POST /clock: line = ");
    debug_log(decoded_line);
    debug_log("\n");

    extern ds3231_t ds3231;
    ds3231_data_t old_time, new_time;

    ds3231_read_current_time(&ds3231, &old_time);
    set_rtc_from_display_string(&ds3231, decoded_line);
    ds3231_read_current_time(&ds3231, &new_time);

    int old_min = old_time.hours * 60 + old_time.minutes;
    int new_min = new_time.hours * 60 + new_time.minutes;
    int delta = new_min - old_min;

    char msg[256];
    snprintf(msg, sizeof(msg),
             "✔️ Uhrzeit gesetzt<br>"
             "Vorher: %02d:%02d&nbsp;am&nbsp;%02d.%02d.%04d<br>"
             "Jetzt: %02d:%02d&nbsp;am&nbsp;%02d.%02d.%04d<br>"
             "Differenz: <b>%d&nbsp;Minute%s</b>",
             old_time.hours, old_time.minutes, old_time.date, old_time.month, old_time.year + 2000,
             new_time.hours, new_time.minutes, new_time.date, new_time.month, new_time.year + 2000,
             abs(delta), abs(delta) == 1 ? "" : "n");

    send_clock_page(tpcb, msg);
}

static void handle_post_clockcopied(struct tcp_pcb* tpcb, struct pbuf* p, const char* buffer, int copied) {
    const char *cl = strstr(upload_session.header_buffer, "Content-Length:");
    if (!cl) {
        debug_log_with_color(COLOR_RED, "UPLOAD CLOCK: Content-Length fehlt\n");
        send_clock_page(tpcb, "❌ Content-Length fehlt.");
        tcp_close(tpcb);
        return;
    }

    upload_session.expected_length = atoi(cl + 15);
    if (upload_session.expected_length >= sizeof(upload_session.form_buffer)) {
        debug_log_with_color(COLOR_RED, "UPLOAD CLOCK: body zu groß\n");
        send_clock_page(tpcb, "❌ Formulardaten zu groß.");
        tcp_close(tpcb);
        return;
    }

    upload_session.active = true;
    upload_session.total_received = 0;
    upload_session.type = UPLOAD_FORM_CLOCK;

    const char *body = strstr(upload_session.header_buffer, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t body_len = upload_session.header_length - (body - upload_session.header_buffer);
        memcpy(upload_session.form_buffer, body, body_len);
        upload_session.total_received = body_len;
        tcp_recved(tpcb, copied);
        debug_log("UPLOAD CLOCK: First body chunk (%d bytes)\n", (int)body_len);

        if (upload_session.total_received >= upload_session.expected_length) {
            upload_session.form_buffer[upload_session.expected_length] = '\0';
            handle_form_clock(tpcb, upload_session.form_buffer, upload_session.expected_length);
            upload_session.active = false;
            upload_session.header_complete = false;
            upload_session.header_length = 0;
        }
    }
}

static err_t recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char buffer[1500];
    int copied = pbuf_copy_partial(p, buffer, sizeof(buffer), 0);
    buffer[copied] = '\0';

    // collect header
    if (!upload_session.header_complete) {
        if (upload_session.header_length + copied < sizeof(upload_session.header_buffer)) {
            memcpy(upload_session.header_buffer + upload_session.header_length, buffer, copied);
            upload_session.header_length += copied;
            upload_session.header_buffer[upload_session.header_length] = '\0';

            debug_log("HEADER: collected %d bytes, total %d\n", copied, (int)upload_session.header_length);

            char *end = strstr(upload_session.header_buffer, "\r\n\r\n");
            if (!end) {
                // Noch nicht komplett
                pbuf_free(p);
                return ERR_OK;
            }

            upload_session.header_complete = true;             // Header complete
            const char *route_line = upload_session.header_buffer;
            char *eol = strstr(route_line, "\r\n"); // Extrahiere nur die erste Headerzeile in eine eigene Kopie
            if (!eol) {
                debug_log_with_color(COLOR_RED, "HEADER: malformed - no CRLF\n");
                pbuf_free(p);
                return ERR_OK;
            }

            size_t line_len = eol - route_line;
            if (line_len >= 128) line_len = 127;  // Sicherheitsgrenze

            char first_line[128];
            memcpy(first_line, route_line, line_len);
            first_line[line_len] = '\0';

            debug_log("HEADER LINE: %s\n", first_line);
            // debug_log("HEADER COMPLETE: %s\n", route_line);

            if (strncmp(first_line, "POST /upload_logo", strlen("POST /upload_logo")) == 0) {
                debug_log("UPLOAD: Detected /upload_logo route\n");
                handle_post_upload_logo(tpcb, p, buffer, copied);
            }
            else if (strncmp(first_line, "POST /firmware_update", strlen("POST /firmware_update")) == 0) {
                debug_log("UPLOAD: Detected POST /firmware_update route\n");
                handle_post_firmware_update(tpcb, p, buffer, copied);
            }
            else if (strncmp(first_line, "POST /wifi", strlen("POST /wifi")) == 0) {
                debug_log("UPLOAD: Detected POST /wifi\n");
                handle_post_wificopied(tpcb, p, buffer, copied);
                pbuf_free(p);
                return ERR_OK;
            }
            else if (strncmp(first_line, "POST /device_config", strlen("POST /device_config")) == 0) {
                debug_log("UPLOAD: Detected POST /device_config\n");
                handle_post_devicecopied(tpcb, p, buffer, copied);
                pbuf_free(p);
                return ERR_OK;
            }
            else if (strncmp(first_line, "POST /seatsurfing", strlen("POST /seatsurfing")) == 0) {
                debug_log("UPLOAD: Detected POST /seatsurfing\n");
                handle_post_seatsurfingcopied(tpcb, p, buffer, copied);
                pbuf_free(p);
                return ERR_OK;
            }
            else if (strncmp(first_line, "POST /delete_logo", strlen("POST /delete_logo")) == 0) {
                uint32_t ints = save_and_disable_interrupts();
                flash_range_erase(LOGO_FLASH_OFFSET, LOGO_FLASH_SIZE);
                restore_interrupts(ints);

                debug_log("UPLOAD: flash erased at address: %d , %d bytes.\n", LOGO_FLASH_OFFSET, LOGO_FLASH_SIZE);

                send_upload_logo_page(tpcb, "<p style='color:orange; font-weight:bold;'>✔️ Logo erfolgreich gelöscht.</p>");
                upload_session.active = false;
                upload_session.header_complete = false;
                upload_session.header_length = 0;
            }
            else if (strncmp(first_line, "POST /clock", strlen("POST /clock")) == 0) {
                debug_log("UPLOAD: Detected POST /clock\n");
                handle_post_clockcopied(tpcb, p, buffer, copied);
                pbuf_free(p);
                return ERR_OK;
            }

            else if (strncmp(first_line, "GET /upload_logo", strlen("GET /upload_logo")) == 0) {
                debug_log("GET /upload_logo aufgerufen\n");
                send_upload_logo_page(tpcb, "");
                tcp_recved(tpcb, copied);
                upload_session.header_complete = false;
                upload_session.header_length = 0;
                pbuf_free(p);
                return ERR_OK;
            }
            else if (strncmp(first_line, "GET /device_status", strlen("GET /device_status")) == 0) {
                debug_log("GET /device_status called\n");
                send_device_status_page(tpcb);
                tcp_recved(tpcb, copied);
                upload_session.header_complete = false;
                upload_session.header_length = 0;
                pbuf_free(p);
                return ERR_OK;
            }
            else if (strncmp(first_line, "GET /device_settings", strlen("GET /device_settings")) == 0) {
                debug_log("GET /device_settings called\n");
                send_device_config_page(tpcb, "");
                tcp_recved(tpcb, copied);
                upload_session.header_complete = false;
                upload_session.header_length = 0;
                pbuf_free(p);
                return ERR_OK;
            }
            else if (strncmp(first_line, "GET /logo", strlen("GET /logo")) == 0) {
                debug_log("GET /logo called\n");
                // handle_logo_request(tpcb);
                tcp_recved(tpcb, copied);
                pbuf_free(p);
                return ERR_OK;
            }
            else if (strncmp(first_line, "GET /firmware_update", strlen("GET /firmware_update")) == 0) {
                debug_log("GET /firmware_update called\n");
                send_firmware_update_page(tpcb, "");
                tcp_recved(tpcb, copied);
                upload_session.header_complete = false;
                upload_session.header_length = 0;
                pbuf_free(p);
                return ERR_OK;
            }
            else if (strncmp(first_line, "GET /wifi", strlen("GET /wifi")) == 0) {
                debug_log("GET /wifi called\n");
                send_wifi_config_page(tpcb, "");
                tcp_recved(tpcb, copied);
                upload_session.header_complete = false;
                upload_session.header_length = 0;
                pbuf_free(p);
                return ERR_OK;
            }
            else if (strncmp(first_line, "GET /shutdown", strlen("GET /shutdown")) == 0) {
                static bool shutdown_triggered = false;
                if (shutdown_triggered) {
                    debug_log("Shutdown bereits in Vorbereitung, Ignorieren\n");
                    tcp_recved(tpcb, copied);
                    pbuf_free(p);
                    return ERR_OK;
                }

                shutdown_triggered = true;

                debug_log("GET /shutdown aufgerufen – Weiterleitung + Shutdown\n");

                send_response(tpcb,
                              "<!DOCTYPE html><html><head>"
                              "<meta charset=\"UTF-8\">"
                              "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                              "<title>Rebooting</title>"
                              "<style>"
                              "body { font-family: sans-serif; text-align: center; padding: 2em; }"
                              "h1 { font-size: 1.5em; color: #333; }"
                              "p { font-size: 1em; color: green; }"
                              "</style></head><body>"
                              "<h1>✔️ Rebooting...</h1>"
                              // "<p></p>"
                              "</body></html>");

                tcp_output(tpcb);

                tcp_recved(tpcb, copied);
                pbuf_free(p);

                add_alarm_in_ms(600, shutdown_callback, NULL, false);

                return ERR_OK;
            }
            else if (strncmp(first_line, "GET / ", strlen("GET / ")) == 0) {
                debug_log("GET / (start page) called\n");
                send_landing_page(tpcb);
                tcp_recved(tpcb, copied);
                upload_session.header_complete = false;
                upload_session.header_length = 0;
                pbuf_free(p);
                return ERR_OK;
            }
            else if (strncmp(first_line, "GET /seatsurfing", strlen("GET /seatsurfing")) == 0) {
                debug_log("GET / seatsurfing called\n");
                send_seatsurfing_config_page(tpcb, "");
                tcp_recved(tpcb, copied);
                upload_session.header_complete = false;
                upload_session.header_length = 0;
                pbuf_free(p);
                return ERR_OK;
            }
            else if (strncmp(first_line, "GET /clock", strlen("GET /clock")) == 0) {
                debug_log("GET / clock called\n");
                send_clock_page(tpcb, "");
                tcp_recved(tpcb, copied);
                upload_session.header_complete = false;
                upload_session.header_length = 0;
                pbuf_free(p);
                return ERR_OK;
            }
            else {
                tcp_recved(tpcb, copied);
                upload_session.header_complete = false;
                upload_session.header_length = 0;
                debug_log_with_color(COLOR_RED, "not implemented! \n");
            }
        } else {
            debug_log_with_color(COLOR_RED, "HEADER: buffer overflow\n");
            pbuf_free(p);
            return ERR_OK;
        }
    } else if (upload_session.active && upload_session.type == UPLOAD_LOGO) {
        size_t to_copy = copied;
        const uint8_t *src = (const uint8_t *)buffer;

        while (to_copy > 0) {
            size_t space = FLASH_PAGE_SIZE - flash_writer.buffer_filled;
            size_t chunk = (to_copy < space) ? to_copy : space;

            memcpy(flash_writer.buffer + flash_writer.buffer_filled, src, chunk);
            flash_writer.buffer_filled += chunk;
            src += chunk;
            to_copy -= chunk;

            if (flash_writer.buffer_filled == FLASH_PAGE_SIZE) {
                flush_page_to_flash();
            }
        }
        upload_session.total_received += copied;
        tcp_recved(tpcb, copied);
        // sleep_ms(5);
        debug_log("UPLOAD LOGO: Additional chunk (%d bytes, total %d)\n", copied, (int)upload_session.total_received);
    }else if (upload_session.active && upload_session.type == UPLOAD_FIRMWARE) {
        size_t to_copy = copied;
        const uint8_t *src = (const uint8_t *)buffer;

        while (to_copy > 0) {
            size_t space = FLASH_PAGE_SIZE - flash_writer.buffer_filled;
            size_t chunk = (to_copy < space) ? to_copy : space;

            memcpy(flash_writer.buffer + flash_writer.buffer_filled, src, chunk);
            flash_writer.buffer_filled += chunk;
            src += chunk;
            to_copy -= chunk;

            if (flash_writer.buffer_filled == FLASH_PAGE_SIZE) {
                flush_page_to_flash();
            }
        }
        upload_session.total_received += copied;
        // upload_session.flash_offset += copied;
        tcp_recved(tpcb, copied);
        // sleep_ms(5);
        // debug_log("UPLOAD FIRMWARE: /*Addition*/al chunk (%d bytes, total %d)\n", copied, (int)upload_session.total_received);
        // Add this static variable at the top of your upload handler (or make it part of upload_session)
        static int last_logged_percent = -10;

        // In your upload handler, inside the loop or after receiving a chunk:
        int percent = (int)((100ULL * upload_session.total_received) / upload_session.expected_length);

        // Only log every 10% step (i.e., 10%, 20%, ..., 100%)
        if (percent >= last_logged_percent + 10) {
            last_logged_percent = percent;
            debug_log("UPLOAD FIRMWARE: Progress = %d%%\n", percent);
        }

    }else if (upload_session.active && upload_session.type == UPLOAD_FORM_WIFI) {
        size_t to_copy = copied;
        if (upload_session.total_received + to_copy > upload_session.expected_length) {
            to_copy = upload_session.expected_length - upload_session.total_received;
        }

        memcpy(upload_session.form_buffer + upload_session.total_received, buffer, to_copy);
        upload_session.total_received += to_copy;
        tcp_recved(tpcb, copied);

        if (upload_session.total_received >= upload_session.expected_length) {
            upload_session.form_buffer[upload_session.expected_length] = '\0';
            handle_form_wifi(tpcb, upload_session.form_buffer, upload_session.expected_length);
            upload_session.active = false;
            upload_session.header_complete = false;
            upload_session.header_length = 0;
        }
    }else if (upload_session.active &&
        (upload_session.type == UPLOAD_FORM_SEATSURFING ||
         upload_session.type == UPLOAD_FORM_DEVICE ||
         upload_session.type == UPLOAD_FORM_CLOCK
        )) {
                size_t to_copy = copied;
            if (upload_session.total_received + to_copy > upload_session.expected_length) {
                to_copy = upload_session.expected_length - upload_session.total_received;
            }

            memcpy(upload_session.form_buffer + upload_session.total_received, buffer, to_copy);
            upload_session.total_received += to_copy;
            tcp_recved(tpcb, copied);

            if (upload_session.total_received >= upload_session.expected_length) {
                upload_session.form_buffer[upload_session.expected_length] = '\0';

                if (upload_session.type == UPLOAD_FORM_SEATSURFING) {
                    handle_form_seatsurfing(tpcb, upload_session.form_buffer, upload_session.expected_length);
                } else if (upload_session.type == UPLOAD_FORM_DEVICE) {
                    handle_form_device_config(tpcb, upload_session.form_buffer, upload_session.expected_length);
                }
                upload_session.active = false;
                upload_session.header_complete = false;
                upload_session.header_length = 0;
            }
        }
    if (upload_session.active && upload_session.total_received >= upload_session.expected_length) {
        debug_log_with_color(COLOR_GREEN, "UPLOAD: Complete (%d bytes)\n", (int)upload_session.total_received);

        flush_page_to_flash();  // letzte unvollständige Seite schreiben
        __dsb();
        __isb();
        // sleep_ms(50);  // Zeit für Cache/Flash-Sync

        // Logging für Debug
        debug_log("FLASH end offset: 0x%X\n", flash_writer.flash_offset);

        if (upload_session.type == UPLOAD_FIRMWARE) {
            char msg[512];
            firmware_header_t header;
            memcpy(&header, FLASH_PTR(upload_session.flash_offset), sizeof(header));

            // Check 1: Magic word
            bool valid = memcmp(header.magic, FIRMWARE_MAGIC, FIRMWARE_MAGIC_LEN) == 0;

            if (!valid) {
                debug_log_with_color(COLOR_RED, "FIRMWARE: Invalid header detected after upload – disabling slot\n");
                send_firmware_update_page(tpcb, "<h2 style='color:red'>❌ FIRMWARE: Invalid header (magic) </h2>");
            }

            // Check that the firmware header claims the correct slot
            uint8_t expected_slot = (upload_session.flash_offset  == FIRMWARE_SLOT0_FLASH_OFFSET) ? 0 :
            (upload_session.flash_offset  == FIRMWARE_SLOT1_FLASH_OFFSET) ? 1 : 255;

            if (valid)
            { // only test slot for acutal firmware (magic word) files
                if (header.slot != expected_slot) {
                    debug_log_with_color(COLOR_RED,
                                        "FIRMWARE: Slot mismatch – header says slot %u, expected slot %u based on upload target 0x%X\n",
                                        header.slot, expected_slot, upload_session.flash_offset );
                    send_firmware_update_page(tpcb, "<h2 style='color:red'>❌ Slot mismatch - invalid firmware!</h2>");
                }
            }

            uint32_t actual_crc;
            if (valid && header.slot == expected_slot)
            { // only test crc32 for actual firmware (magic word) files and correct slot
                const uint8_t *firmware_data = (const uint8_t *)(XIP_BASE + upload_session.flash_offset);
                actual_crc = crc32_calculate(firmware_data, header.firmware_size);

                debug_log("CRC calc: addr = 0x%08X, header.firmware_size = %u\n", (unsigned)(uintptr_t)firmware_data, (unsigned)header.firmware_size);

                if (actual_crc == header.crc32)
                {
                    debug_log("CRC check OK: 0x%08X\n", actual_crc);
                }else
                {
                    debug_log("CRC MISMATCH: expected 0x%08X, got 0x%08X\n", header.crc32, actual_crc);
                    snprintf(msg, sizeof(msg), "<h2 style='color:red'>CRC MISMATCH: expected 0x%08X, got 0x%08X</h2>", header.crc32, actual_crc);
                    send_firmware_update_page(tpcb, msg);
                }
            }
            if (valid && actual_crc == header.crc32 && header.slot == expected_slot) {
                debug_log("Valid Firmware - you may now reboot from the new version!\n");
                mark_firmware_valid(upload_session.flash_offset);

                snprintf(msg, sizeof(msg),
                         "<h2 style='color:green'>Valid Firmware – you may now reboot from the new version!</h2>"
                         "<p>"
                         "Version: <code>%s</code><br>"
                         "Build Date: <code>%s</code><br>"
                         "Size: <code>%u bytes</code><br>"
                         "CRC32: <code>0x%08X</code><br>"
                         "Slot: <code>%u</code>"
                         "</p>",
                         header.git_version,
                         header.build_date,
                         header.firmware_size,
                         header.crc32,
                         header.slot);

                send_firmware_update_page(tpcb, msg);
            }
        }else {
            const char *ok_msg = "<html><body><h2>✅ Upload OK</h2><a href='/'>Zurück</a></body></html>";
            send_response(tpcb, ok_msg);
        }

        reset_upload_session();
        upload_session.active = false;
        upload_session.header_complete = false;
        upload_session.header_length = 0;

        tcp_recved(tpcb, copied);
    }
    pbuf_free(p);
    return ERR_OK;
}

static err_t accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, recv_cb);
    return ERR_OK;
}

void start_setup_webserver() {
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) return;

    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) {
        tcp_close(pcb);
        return;
    }

    pcb = tcp_listen(pcb);
    tcp_accept(pcb, accept_cb);
}

