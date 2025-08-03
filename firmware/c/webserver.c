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

#define USER_INTERACTION_TIMEOUT_MS (5 * 60 * 1000)  // reset active time for user interaction: 5 minutes
#define TCP_CHUNK_SIZE 1024

#include <stdint.h>
#include <stddef.h>

static const uint32_t crc32_table[256] = {
    0x00000000U, 0x77073096U, 0xEE0E612CU, 0x990951BAU, 0x076DC419U, 0x706AF48FU, 0xE963A535U, 0x9E6495A3U,
    0x0EDB8832U, 0x79DCB8A4U, 0xE0D5E91EU, 0x97D2D988U, 0x09B64C2BU, 0x7EB17CBDU, 0xE7B82D07U, 0x90BF1D91U,
    0x1DB71064U, 0x6AB020F2U, 0xF3B97148U, 0x84BE41DEU, 0x1ADAD47DU, 0x6DDDE4EBU, 0xF4D4B551U, 0x83D385C7U,
    0x136C9856U, 0x646BA8C0U, 0xFD62F97AU, 0x8A65C9ECU, 0x14015C4FU, 0x63066CD9U, 0xFA0F3D63U, 0x8D080DF5U,
    0x3B6E20C8U, 0x4C69105EU, 0xD56041E4U, 0xA2677172U, 0x3C03E4D1U, 0x4B04D447U, 0xD20D85FDU, 0xA50AB56BU,
    0x35B5A8FAU, 0x42B2986CU, 0xDBBBC9D6U, 0xACBCF940U, 0x32D86CE3U, 0x45DF5C75U, 0xDCD60DCFU, 0xABD13D59U,
    0x26D930ACU, 0x51DE003AU, 0xC8D75180U, 0xBFD06116U, 0x21B4F4B5U, 0x56B3C423U, 0xCFBA9599U, 0xB8BDA50FU,
    0x2802B89EU, 0x5F058808U, 0xC60CD9B2U, 0xB10BE924U, 0x2F6F7C87U, 0x58684C11U, 0xC1611DABU, 0xB6662D3DU,
    0x76DC4190U, 0x01DB7106U, 0x98D220BCU, 0xEFD5102AU, 0x71B18589U, 0x06B6B51FU, 0x9FBFE4A5U, 0xE8B8D433U,
    0x7807C9A2U, 0x0F00F934U, 0x9609A88EU, 0xE10E9818U, 0x7F6A0DBBU, 0x086D3D2DU, 0x91646C97U, 0xE6635C01U,
    0x6B6B51F4U, 0x1C6C6162U, 0x856530D8U, 0xF262004EU, 0x6C0695EDU, 0x1B01A57BU, 0x8208F4C1U, 0xF50FC457U,
    0x65B0D9C6U, 0x12B7E950U, 0x8BBEB8EAU, 0xFCB9887CU, 0x62DD1DDFU, 0x15DA2D49U, 0x8CD37CF3U, 0xFBD44C65U,
    0x4DB26158U, 0x3AB551CEU, 0xA3BC0074U, 0xD4BB30E2U, 0x4ADFA541U, 0x3DD895D7U, 0xA4D1C46DU, 0xD3D6F4FBU,
    0x4369E96AU, 0x346ED9FCU, 0xAD678846U, 0xDA60B8D0U, 0x44042D73U, 0x33031DE5U, 0xAA0A4C5FU, 0xDD0D7CC9U,
    0x5005713CU, 0x270241AAU, 0xBE0B1010U, 0xC90C2086U, 0x5768B525U, 0x206F85B3U, 0xB966D409U, 0xCE61E49FU,
    0x5EDEF90EU, 0x29D9C998U, 0xB0D09822U, 0xC7D7A8B4U, 0x59B33D17U, 0x2EB40D81U, 0xB7BD5C3BU, 0xC0BA6CADU,
    0xEDB88320U, 0x9ABFB3B6U, 0x03B6E20CU, 0x74B1D29AU, 0xEAD54739U, 0x9DD277AFU, 0x04DB2615U, 0x73DC1683U,
    0xE3630B12U, 0x94643B84U, 0x0D6D6A3EU, 0x7A6A5AA8U, 0xE40ECF0BU, 0x9309FF9DU, 0x0A00AE27U, 0x7D079EB1U,
    0xF00F9344U, 0x8708A3D2U, 0x1E01F268U, 0x6906C2FEU, 0xF762575DU, 0x806567CBU, 0x196C3671U, 0x6E6B06E7U,
    0xFED41B76U, 0x89D32BE0U, 0x10DA7A5AU, 0x67DD4ACCU, 0xF9B9DF6FU, 0x8EBEEFF9U, 0x17B7BE43U, 0x60B08ED5U,
    0xD6D6A3E8U, 0xA1D1937EU, 0x38D8C2C4U, 0x4FDFF252U, 0xD1BB67F1U, 0xA6BC5767U, 0x3FB506DDU, 0x48B2364BU,
    0xD80D2BDAU, 0xAF0A1B4CU, 0x36034AF6U, 0x41047A60U, 0xDF60EFC3U, 0xA867DF55U, 0x316E8EEFU, 0x4669BE79U,
    0xCB61B38CU, 0xBC66831AU, 0x256FD2A0U, 0x5268E236U, 0xCC0C7795U, 0xBB0B4703U, 0x220216B9U, 0x5505262FU,
    0xC5BA3BBEU, 0xB2BD0B28U, 0x2BB45A92U, 0x5CB36A04U, 0xC2D7FFA7U, 0xB5D0CF31U, 0x2CD99E8BU, 0x5BDEAE1DU,
    0x9B64C2B0U, 0xEC63F226U, 0x756AA39CU, 0x026D930AU, 0x9C0906A9U, 0xEB0E363FU, 0x72076785U, 0x05005713U,
    0x95BF4A82U, 0xE2B87A14U, 0x7BB12BAEU, 0x0CB61B38U, 0x92D28E9BU, 0xE5D5BE0DU, 0x7CDCEFB7U, 0x0BDBDF21U,
    0x86D3D2D4U, 0xF1D4E242U, 0x68DDB3F8U, 0x1FDA836EU, 0x81BE16CDU, 0xF6B9265BU, 0x6FB077E1U, 0x18B74777U,
    0x88085AE6U, 0xFF0F6A70U, 0x66063BCAU, 0x11010B5CU, 0x8F659EFFU, 0xF862AE69U, 0x616BFFD3U, 0x166CCF45U,
    0xA00AE278U, 0xD70DD2EEU, 0x4E048354U, 0x3903B3C2U, 0xA7672661U, 0xD06016F7U, 0x4969474DU, 0x3E6E77DBU,
    0xAED16A4AU, 0xD9D65ADCU, 0x40DF0B66U, 0x37D83BF0U, 0xA9BCAE53U, 0xDEBB9EC5U, 0x47B2CF7FU, 0x30B5FFE9U,
    0xBDBDF21CU, 0xCABAC28AU, 0x53B39330U, 0x24B4A3A6U, 0xBAD03605U, 0xCDD70693U, 0x54DE5729U, 0x23D967BFU,
    0xB3667A2EU, 0xC4614AB8U, 0x5D681B02U, 0x2A6F2B94U, 0xB40BBE37U, 0xC30C8EA1U, 0x5A05DF1BU, 0x2D02EF8DU
};

extern const uint32_t crc32_table[256];

uint32_t crc32_calculate(const uint8_t *data, size_t length) {
    const size_t skip_header = 256;  // Skip the firmware header (first 256 bytes)

    // Reject if the length is too small to contain a header + payload
    if (length <= skip_header) {
        return 0;
    }

    // Reject if the payload exceeds allowed firmware size
    if ((length) > FIRMWARE_FLASH_SIZE) {
        return 0;
    }

    // Validate that the data pointer lies within the XIP address space of slot 0 or 1
    uintptr_t addr = (uintptr_t)data;
    uintptr_t addr_end = addr + length;

    // Reject on address overflow
    if (addr_end < addr) {
        return 0;
    }

    // XIP-mapped address ranges for slot 0 and slot 1
    uintptr_t slot0_start = (uintptr_t)FLASH_PTR(FIRMWARE_SLOT0_FLASH_OFFSET);
    uintptr_t slot0_end   = slot0_start + FIRMWARE_FLASH_SIZE;

    uintptr_t slot1_start = (uintptr_t)FLASH_PTR(FIRMWARE_SLOT1_FLASH_OFFSET);
    uintptr_t slot1_end   = slot1_start + FIRMWARE_FLASH_SIZE;

    // Check if data lies entirely within one of the valid slots
    int valid_slot0 = (addr >= slot0_start) && (addr_end <= slot0_end);
    int valid_slot1 = (addr >= slot1_start) && (addr_end <= slot1_end);

    if (!valid_slot0 && !valid_slot1) {
        return 0;
    }

    // Adjust for skipped header
    data += skip_header;
    length -= skip_header;

    // Compute CRC32 using standard polynomial
    uint32_t crc = 0xFFFFFFFF;
    while (length--) {
        crc = crc32_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

void reset_upload_session(void) {
    upload_session.active = false;
    upload_session.header_complete = false;
    upload_session.header_length = 0;
    upload_session.total_received = 0;
    upload_session.expected_length = 0;
    upload_session.flash_offset = 0;
    upload_session.type = UPLOAD_NONE;
}

void safe_flash_copy(void* dest, const void* flash_src, size_t len) {
    const volatile uint8_t* src = (const volatile uint8_t*)flash_src;
    uint8_t* dst = (uint8_t*)dest;
    for (size_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
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
void url_decode(char *dst, const char *src, size_t dst_len) {
    char a, b;
    size_t i = 0;

    while (*src && i + 1 < dst_len) {
        if (*src == '%') {
            if ((a = src[1]) && (b = src[2]) && isxdigit(a) && isxdigit(b)) {
                // Umwandlung von zwei Hex-Zeichen in ein Byte
                a = (char)(isdigit(a) ? a - '0' : toupper(a) - 'A' + 10);
                b = (char)(isdigit(b) ? b - '0' : toupper(b) - 'A' + 10);
                dst[i++] = (char)(16 * a + b);
                src += 3;
            } else {
                // Ungültige Kodierung, '%' direkt übernehmen
                dst[i++] = *src++;
            }
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

void webserver_set_shutdown_time(absolute_time_t t) {
    shutdown_time = t;
}
static void add_timeout_info(char *buf, size_t buf_size) {
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

void send_device_status_page(struct tcp_pcb* tpcb) {
    char page[4096];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    char buffer[1024];
    char buffer2[1024];
    uint8_t mac_address[6];

    extern ds3231_t ds3231;
    ds3231_data_t now;

    // Get MAC address using public cyw43_state
    if (cyw43_wifi_get_mac(&cyw43_state, 0, mac_address) == 0) {
        // success: mac_address[0]..[5] now valid
    } else {
        // fallback: e.g. clear array or mark as invalid
        memset(mac_address, 0, sizeof(mac_address));
    }

    ds3231_read_current_time(&ds3231, &now);
    float vcc = read_battery_voltage(device_config_flash.data.conversion_factor);
    float vbat = read_coin_cell_voltage(device_config_flash.data.conversion_factor);

    // // Spannungsbewertung (für einfache Farbcodierung)
    const char* vcc_color = (vcc > 3.5) ? "green" : (vcc > 3.0 ? "orange" : "red");
    const char* vbat_color = (vbat > 3.1) ? "green" : (vbat > 2.9 ? "orange" : "red");

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta charset=\"UTF-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
             "<meta http-equiv=\"refresh\" content=\"300\">"
             "<title>Device Status</title>"
             "<style>"
             "body { font-family: sans-serif; text-align: center; padding: 1em; }"
             ".value { font-weight: bold; }"
             ".green { color: green; }"
             ".orange { color: orange; }"
             ".red { color: red; }"
             ".section { margin-bottom: 1.2em; }"
             "a { display: inline-block; margin-top: 2em; text-decoration: none; color: #0066cc; }"
             "</style></head><body>"
             "<h1>Device Status</h1>");

    // Raumname & SSID
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>Room: <span class='value'>%s</span><br>"
             "SSID: <span class='value'>%s</span></div>",
             device_config_flash.data.roomname,
             wifi_config_flash.ssid);
    strcat(page, buffer);

    // Wi-Fi Einstellungen
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>Reconnect Interval: <span class='value'>%d min</span><br>"
             "Wi-Fi Timeout: <span class='value'>%d s</span></div>",
             device_config_flash.data.wifi_reconnect_minutes,
             device_config_flash.data.wifi_timeout);
    strcat(page, buffer);

    // Refresh-Minuten
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>Refresh Intervals:<br><span class='value'>[%d, %d, %d, %d, %d, %d, %d, %d]</span></div>",
             device_config_flash.data.refresh_minutes_by_pushbutton[0],
             device_config_flash.data.refresh_minutes_by_pushbutton[1],
             device_config_flash.data.refresh_minutes_by_pushbutton[2],
             device_config_flash.data.refresh_minutes_by_pushbutton[3],
             device_config_flash.data.refresh_minutes_by_pushbutton[4],
             device_config_flash.data.refresh_minutes_by_pushbutton[5],
             device_config_flash.data.refresh_minutes_by_pushbutton[6],
             device_config_flash.data.refresh_minutes_by_pushbutton[7]);
    strcat(page, buffer);

    // RTC Zeit (roh)
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>RTC (raw): <span class='value'>%02d:%02d, %s, %02d. %s %04d</span></div>",
             now.hours, now.minutes,
             get_day_of_week(now.day),
             now.date, get_month_name(now.month),
             2000 + now.year);
    strcat(page, buffer);

    // RTC Zeit (DST)
    format_rtc_time(&now, buffer2, sizeof(buffer2));
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>RTC (DST): <span class='value'>%s</span></div>", buffer2);
    strcat(page, buffer);

    // MAC Adresse
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>MAC address: <span class='value'>%02X:%02X:%02X:%02X:%02X:%02X</span></div>",
             mac_address[0], mac_address[1], mac_address[2],
             mac_address[3], mac_address[4], mac_address[5]);
    strcat(page, buffer);

    // Spannungen
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>"
             "Vcc: <span class='value %s'>%.3f V</span><br>"
             "Vbat: <span class='value %s'>%.3f V</span><br>"
             "ADC Conversion Factor: <span class='value'>%.8f</span></div>",
             vcc_color, vcc,
             vbat_color, vbat,
             device_config_flash.data.conversion_factor);
    strcat(page, buffer);

    // Logo-Flash-Info :
    int logo_width = 0, logo_height = 0, logo_size = 0;
    if (get_flash_logo_info(&logo_width, &logo_height, &logo_size)) {
        snprintf(buffer, sizeof(buffer),
                 "<div class='section'>Logo in flash:<br>"
                 "<span class='value'>%dx%d px, %d Bytes</span></div>",
                 logo_width, logo_height, logo_size);
    } else {
        snprintf(buffer, sizeof(buffer),
                 "<div class='section'>Logo in flash: <span class='value red'>not present</span></div>");
    }
    strcat(page, buffer);

// active slot and reset pointer
    snprintf(buffer, sizeof(buffer),
             "<div class='section'>Aktive Firmware:<br>"
             "<div><span class='value'>%s</span></div><br>",
             get_active_firmware_slot_info());

    strcat(page, buffer);

    char build0[16] = {0}, version0[32] = {0};
    char build1[16] = {0}, version1[32] = {0};
    uint32_t size0 = 0, size1 = 0;
    uint32_t crc0 = 0, crc1 = 0;
    uint8_t slot_index0 = 0, slot_index1 = 0;
    uint8_t valid0 = 0, valid1 = 0;

    bool has0 = get_firmware_slot_info(0, build0, version0, &size0, &crc0, &slot_index0, &valid0);
    bool has1 = get_firmware_slot_info(1, build1, version1, &size1, &crc1, &slot_index1, &valid1);

    // Slot 0 HTML
    if (has0) {
        snprintf(buffer, sizeof(buffer),
                 "<div>Slot 0:</div>\n"
                 "<div>Version: <span class='value'>%s</span></div>\n"
                 "<div>Build: <span class='value'>%s</span></div>\n"
                 "<div>Größe: <span class='value'>%u Bytes</span></div>\n"
                 "<div>CRC32: <span class='value'>0x%08X</span></div>\n"
                 "<div>Slot: <span class='value'>%u</span></div>\n"
                 "<div>Valid: <span class='value'>%u</span></div><br>\n\n\n",
                 version0, build0, size0, crc0, slot_index0, valid0);
    } else {
        snprintf(buffer, sizeof(buffer),
                 "<li>Slot 0: <span class='value red'>leer oder ungültig</span></li>\n");
    }
    strcat(page, buffer);

    // Slot 1 HTML
    if (has1) {
        snprintf(buffer, sizeof(buffer),
                 "<div>Slot 1:</div>\n"
                 "<div>Version: <span class='value'>%s</span></div>\n"
                 "<div>Build: <span class='value'>%s</span></div>\n"
                 "<div>Größe: <span class='value'>%u Bytes</span></div>\n"
                 "<div>CRC32: <span class='value'>0x%08X</span></div>\n"
                 "<div>Slot: <span class='value'>%u</span></div>\n"
                 "<div>Valid: <span class='value'>%u</span></div>\n",
                 version1, build1, size1, crc1, slot_index1, valid1);
    } else {
        snprintf(buffer, sizeof(buffer),
                 "<li>Slot 1: <span class='value red'>leer oder ungültig</span></li>\n");
    }
    strcat(page, buffer);

        // Liste schließen
        strcat(page, "</ul></div>\n");

    // Zurück-Link
    strcat(page, "<a href=\"/\">back</a></body></html>");

    send_response(tpcb, page);
}

void send_upload_logo_page(struct tcp_pcb* tpcb, const char* message) {
    char page[4096];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width, initial-scale=1'>"
             // "<meta http-equiv=\"refresh\" content=\"5\">"
             "<title>Logo Upload</title>"
             "<style>"
             "body { font-family: sans-serif; text-align: center; margin: 2em; }"
             "input[type='file'] { font-size: 1em; padding: 0.5em; margin: 0.5em auto; display: block; width: 80%%; max-width: 300px; }"
             "button { font-size: 1em; padding: 0.5em; margin: 0.5em auto; display: block; width: 80%%; max-width: 300px; }"
             "#status, #error { margin-top: 1em; font-weight: bold; color: red; }"
             "progress { width: 80%%; max-width: 300px; height: 2em; margin-top: 1em; }"
             "a { display: inline-block; margin-top: 2em; }"
             "</style>"
             "</head><body>"
             "<h1>Upload Logo</h1>"
             "%s"
             "<input type='file' id='fileInput'><br>"
             "<button onclick='upload()'>Upload</button><br>"
             "<progress id='progressBar' max='100' value='0'></progress>"
             "<p id='status'></p>"
             "<a href='/'>Zurück</a>"
             "<script>"
             "const MAX_SIZE = %d;"  // Wird korrekt ersetzt
             "function upload() {"
             "  const file = document.getElementById('fileInput').files[0];"
             "  if (!file) return;"
             "  if (file.size > MAX_SIZE) {"
             "    document.getElementById('status').innerText = '❌ Datei zu groß (' + file.size + ' Bytes, maximal ' + MAX_SIZE + ' Bytes erlaubt)';"
             "    return;"
             "  }"
             "  const xhr = new XMLHttpRequest();"
             "  xhr.open('POST', '/upload_logo', true);"
             "  xhr.setRequestHeader('Content-Type', 'application/octet-stream');"
             "  xhr.upload.onprogress = function(e) {"
             "    if (e.lengthComputable) {"
             "      const percent = Math.round(e.loaded / e.total * 100);"
             "      document.getElementById('progressBar').value = percent;"
             "      document.getElementById('status').innerText = 'Hochladen: ' + percent + '%%';"
             "    }"
             "  };"
             "  xhr.onload = function() {"
             "    if (xhr.status == 200) document.getElementById('status').innerText = '✅ Upload OK';"
             "    else document.getElementById('status').innerText = '❌ Upload fehlgeschlagen';"
             "  };"
             "  xhr.onerror = function() {"
             "    document.getElementById('status').innerText = '❌ Fehler beim Upload';"
             "  };"
             "  xhr.send(file);"
             "}"
             "</script></body></html>",
             (message && *message) ? message : "",
             LOGO_FLASH_SIZE
    );

    int width, height, datalen;
    bool has_logo = get_flash_logo_info(&width, &height, &datalen);

    if (has_logo) {
        snprintf(page + strlen(page), sizeof(page) - strlen(page),
                 "<p><b>Benutzerdefiniertes Logo gefunden:</b> %d×%d Pixel, %d Bytes</p>\n"
                 "<form method=\"POST\" action=\"/delete_logo\">"
                 "<button type=\"submit\">delete logo</button></form>\n",
                 width, height, datalen);
    } else {
        strcat(page, "<p><i>Kein benutzerdefiniertes Logo im Flash.</i></p>\n");
    }


    snprintf(page + strlen(page), sizeof(page) - strlen(page),
             "<p>%s</p></body></html>", timeout_info);
    debug_log("upload_logo page length: %d\n", strlen(page));

    send_response(tpcb, page);
}

void send_firmware_update_page(struct tcp_pcb* tpcb, const char* message) {
    // Wenn `message` mit "<div" oder "<h2" beginnt, nur das Fragment senden
    if (message && *message && (
        strncmp(message, "<div", 4) == 0 ||
        strncmp(message, "<h2", 3) == 0)) {
        debug_log("Sending short message only (HTML fragment)\n");
    send_response(tpcb, message);
    return;
        }

        // Normale vollständige Seite
        char page[4096];
        char buffer[256];
        char timeout_info[64];
        add_timeout_info(timeout_info, sizeof(timeout_info));
        const int max_size = FIRMWARE_FLASH_SIZE;
        const int duration = upload_session.flash_estimated_duration > 0 ? upload_session.flash_estimated_duration : 15000;

        snprintf(page, sizeof(page),
                 "<!DOCTYPE html><html><head>"
                 "<meta charset=\"UTF-8\">"
                 "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                 "<title>firmware update</title>"
                 "<style>"
                 "body { font-family: sans-serif; text-align: center; margin: 2em; }"
                 "input[type='file'] { font-size: 1em; padding: 0.5em; margin: 0.5em auto; display: block; width: 80%%; max-width: 300px; }"
                 "button { font-size: 1em; padding: 0.5em; margin: 0.5em auto; display: block; width: 80%%; max-width: 300px; }"
                 "#status { margin-top: 1em; font-weight: bold; color: red; }"
                 "progress { width: 80%%; max-width: 300px; height: 2em; margin-top: 1em; }"
                 "a { display: inline-block; margin-top: 2em; }"
                 "</style></head><body>\n");

        strcat(page, "<h1>Firmware Update</h1>");

        // Active slot info
        snprintf(buffer, sizeof(buffer),
                 "<div class='section'>Active firmware:<br>"
                 "<div><span class='value'>%s</span></div><br>",
                 get_active_firmware_slot_info());
        strcat(page, buffer);

        if (message && *message) {
            strncat(page, message, sizeof(page) - strlen(page) - 1);
        }

        strcat(page,
               "<input type='file' id='fileInput'><br>"
               "<button onclick='upload()'>Upload</button><br>"
               "<progress id='progressBar' max='100' value='0'></progress>"
               "<p id='status'></p>"
               "<div id='uploadResult'></div>"
               "<a href='/'>Zurück</a>\n");

        snprintf(page + strlen(page), sizeof(page) - strlen(page),
                 "<script>"
                 "const MAX_SIZE = %d;"
                 "let interval = null;"
                 "function simulateFlashingProgress(durationMs = %d) {"
                 "  let startTime = Date.now();"
                 "  interval = setInterval(() => {"
                 "    const elapsed = Date.now() - startTime;"
                 "    let percent = Math.min(100, Math.round(elapsed / durationMs * 100));"
                 "    document.getElementById('progressBar').value = percent;"
                 "    document.getElementById('status').innerText = 'Flashen: ' + percent + '%%';"
                 "    if (percent >= 100) {"
                 "      clearInterval(interval);"
                 "      document.getElementById('progressBar').value = 100;"
                 "      document.getElementById('status').innerText = '❌ timeout';"
                 "    }"
                 "  }, 300);"
                 "}"

                 "function upload() {"
                 "  const file = document.getElementById('fileInput').files[0];"
                 "  if (!file) return;"
                 "  if (file.size > MAX_SIZE) {"
                 "    document.getElementById('status').innerText = '❌ Datei zu groß (' + file.size + ' Bytes, maximal ' + MAX_SIZE + ' Bytes erlaubt)';"
                 "    return;"
                 "  }"
                 "  const xhr = new XMLHttpRequest();"
                 "  xhr.open('POST', '/firmware_update', true);"
                 "  xhr.setRequestHeader('Content-Type', 'application/octet-stream');"
                 "  xhr.responseType = 'text';"
                 "  xhr.onerror = function() {"
                 "    clearInterval(interval);"
                 "    document.getElementById('status').innerText = '❌ Fehler beim Upload';"
                 "  };"
                 "  xhr.onload = function() {"
                 "    clearInterval(interval);"
                 "    document.getElementById('progressBar').value = 100;"
                 "    document.getElementById('status').innerText = '';"
                 "    document.getElementById('uploadResult').innerHTML = xhr.responseText;"
                 "  };"
                 "  xhr.send(file);"
                 "  simulateFlashingProgress();"
                 "}"
                 "</script></body></html>",
                 max_size, duration);

        // Firmware Slot Info
        char build0[16] = {0}, version0[32] = {0};
        char build1[16] = {0}, version1[32] = {0};
        uint32_t size0 = 0, size1 = 0;
        uint32_t crc0 = 0, crc1 = 0;
        uint8_t slot_index0 = 0, slot_index1 = 0;
        uint8_t valid0 = 0, valid1 = 0;

        bool has0 = get_firmware_slot_info(0, build0, version0, &size0, &crc0, &slot_index0, &valid0);
        bool has1 = get_firmware_slot_info(1, build1, version1, &size1, &crc1, &slot_index1, &valid1);

        if (has0 || has1) {
            strcat(page, "<p><b>Firmware im Flash gefunden:</b></p><ul>\n");

            if (has0) {
                snprintf(page + strlen(page), sizeof(page) - strlen(page),
                         "<div>Slot 0: %s (%s), %u Bytes</div>\n",
                         version0, build0, size0);
            } else {
                strcat(page, "<div>Slot 0: <i>leer oder ungültig</i></div>\n");
            }

            if (has1) {
                snprintf(page + strlen(page), sizeof(page) - strlen(page),
                         "<div>Slot 1: %s (%s), %u Bytes</div>\n",
                         version1, build1, size1);
            } else {
                strcat(page, "<div>Slot 1: <i>leer oder ungültig</i></div>\n");
            }

            strcat(page, "</ul>\n");
        } else {
            strcat(page, "<p><i>Keine gültige Firmware in Slot 0 oder 1 gefunden.</i></p>\n");
        }

        snprintf(page + strlen(page), sizeof(page) - strlen(page),
                 "<p>%s</p></body></html>", timeout_info);

        debug_log("firmware_update page length: %d\n", strlen(page));
        send_response(tpcb, page);
}

void send_landing_page(struct tcp_pcb *tpcb) {
    char page[4096];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta charset=\"UTF-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
             "<meta http-equiv=\"refresh\" content=\"5\">"
             "<title>inki Setup</title>"
             "<style>"
             "body { font-family: sans-serif; text-align: center; }"
             "a { display: inline-block; padding: 0.6em 1em; font-size: 1em; margin: 0.5em; width: 80%%; max-width: 200px; background: #eee; border: 1px solid #ccc; border-radius: 5px; text-decoration: none; color: black; }"
             "a:hover { background: #ddd; }"
             "p { margin-top: 2em; font-size: 0.9em; }"
             "</style></head><body>\n");

    strcat(page,
           "<h1>inki Setup</h1>"
           "<a href=\"/wifi\">Wi-Fi Settings</a><br>"
           "<a href=\"/seatsurfing\">Seatsurfing Settings</a><br>"
           "<a href=\"/device_settings\">Device Settings</a><br>"
           "<a href=\"/upload_logo\">Upload Logo</a><br>"
           "<a href=\"/device_status\">Device Status</a><br>"
           "<a href=\"/firmware_update\">Firmware Update</a><br>"
           "<a href=\"/clock\">Set Clock</a><br>"
           "<a href=\"/shutdown\">Reboot</a>");
/*
    strcat(page,
           "<img src=\"/logo\" alt=\"inki logo\" width=\"104\" height=\"95\"><br>");*/

    snprintf(page + strlen(page), sizeof(page) - strlen(page),
             "<p>%s</p></body></html>", timeout_info);
    debug_log("Landing page length: %d\n", strlen(page));

    send_response(tpcb, page);
}

void send_wifi_config_page(struct tcp_pcb *tpcb, const char *message) {
    char page[2048];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta charset=\"UTF-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
             "<meta http-equiv=\"refresh\" content=\"300\">"
             "<title>Wi-Fi Konfiguration</title>"
             "<style>"
             "body { font-family: sans-serif; text-align: center; }"
             "form { max-width: 400px; margin: auto; padding: 1em; }"
             "label { display: block; margin-bottom: 1em; font-size: 1em; }"
             "input[type='text'] { width: 100%%; padding: 0.5em; font-size: 1em; }"
             "input[type='submit'] { padding: 0.6em 1em; font-size: 1em; margin: 0.5em; width: 45%%; max-width: 150px; }"
             "a { display: inline-block; margin-top: 1.5em; font-size: 0.9em; text-decoration: none; color: #0066cc; }"
             ".message { margin: 1em auto; font-size: 1em; font-weight: bold; color: green; }"
             "</style></head><body>"
             "<h1>Wi-Fi Konfiguration</h1>");

    if (message && *message) {
        snprintf(page + strlen(page), sizeof(page) - strlen(page),
                 "<div class='message'>%s</div>", message);
    }

    snprintf(page + strlen(page), sizeof(page) - strlen(page),
             "<form method=\"POST\" action=\"/wifi\">"
             "<label>SSID:<br><input type=\"text\" name=\"text1\" value=\"%s\"></label>"
             "<label>Passwort:<br><input type=\"text\" name=\"text2\" value=\"%s\"></label>"
             "<input type=\"submit\" value=\"store\">"
             "</form>"
             "<a href=\"/\">back</a>"
             "<p>%s</p>"
             "</body></html>",
             wifi_config_flash.ssid,
             wifi_config_flash.password,
             timeout_info);

    send_response(tpcb, page);
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

void send_seatsurfing_config_page(struct tcp_pcb* tpcb, const char* message) {
    char page[2048];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    char ip_string[16];
    snprintf(ip_string, sizeof(ip_string), "%d.%d.%d.%d",
             seatsurfing_config_flash.data.ip[0],
             seatsurfing_config_flash.data.ip[1],
             seatsurfing_config_flash.data.ip[2],
             seatsurfing_config_flash.data.ip[3]);

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta charset=\"UTF-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
             "<meta http-equiv=\"refresh\" content=\"30\">"
             "<title>Seatsurfing Konfiguration</title>"
             "<style>"
             "body { font-family: sans-serif; text-align: center; }"
             "form { max-width: 400px; margin: auto; padding: 1em; }"
             "label { display: block; margin-bottom: 1em; font-size: 1em; }"
             "input[type='text'] { width: 100%%; padding: 0.5em; font-size: 1em; }"
             "input[type='submit'] { padding: 0.6em 1em; font-size: 1em; margin: 0.5em; width: 45%%; max-width: 150px; }"
             "a { display: inline-block; margin-top: 1.5em; font-size: 0.9em; text-decoration: none; color: #0066cc; }"
             "</style></head><body>"
             "<h1>Seatsurfing Konfiguration</h1>");

            if (message && *message) {
                snprintf(page + strlen(page), sizeof(page) - strlen(page),
                        "<div class='message'>%s</div>", message);
            }

             snprintf(page + strlen(page), sizeof(page) - strlen(page),
             "<form method=\"POST\" action=\"/seatsurfing\">"
             "<label>API Host:<br><input type=\"text\" name=\"text1\" value=\"%s\"></label>"
             "<label>Benutzername:<br><input type=\"text\" name=\"text2\" value=\"%s\"></label>"
             "<label>Passwort:<br><input type=\"text\" name=\"text3\" value=\"%s\"></label>"
             "<label>IP-Adresse:<br><input type=\"text\" name=\"text4\" value=\"%s\"></label>"
             "<label>Port:<br><input type=\"text\" name=\"text5\" value=\"%d\"></label>"
             "<label>Space ID:<br><input type=\"text\" name=\"text6\" value=\"%s\"></label>"
             "<label>Location ID:<br><input type=\"text\" name=\"text7\" value=\"%s\"></label>"
             "<input type=\"submit\" value=\"store\">"
             "</form>"
             "<a href=\"/\">Zurück zum Start</a>"
             "<p>%s</p>"
             "</body></html>",
             seatsurfing_config_flash.data.host,
             seatsurfing_config_flash.data.username,
             seatsurfing_config_flash.data.password,
             ip_string,
             seatsurfing_config_flash.data.port,
             seatsurfing_config_flash.data.space_id,
             seatsurfing_config_flash.data.location_id,
             timeout_info);

    send_response(tpcb, page);
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

void send_clock_page(struct tcp_pcb *tpcb, const char *message) {
    char page[8192];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    extern ds3231_t ds3231;
    ds3231_data_t current;
    ds3231_read_current_time(&ds3231, &current);

    // Zeit als Rohwert (RTC pur)
    char current_raw[64];
    snprintf(current_raw, sizeof(current_raw), "%02d:%02d:%02d %02d.%02d.%04d",
             current.hours, current.minutes, current.seconds,
             current.date, current.month, current.year + 2000);

    // Zeit inkl. Sommerzeit
    char current_dst[64];
    format_rtc_time(&current, current_dst, sizeof(current_dst));

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta charset=\"UTF-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
             "<meta http-equiv=\"refresh\" content=\"300\">"
             "<title>Uhrzeit setzen</title>"
             "<style>"
             "body { font-family: sans-serif; text-align: center; }"
             "form { margin-top: 2em; }"
             "input[type='submit'] { padding: 0.6em 1em; font-size: 1em; margin-top: 1em; }"
             ".message { margin: 1em auto; font-size: 1em; font-weight: bold; color: green; }"
             ".section { margin: 1em 0; font-size: 1.1em; }"
             ".value { font-weight: bold; }"
             "</style></head><body>"
             "<h1>Uhrzeit setzen</h1>"

             "<div class='section'>RTC (roh): <span class='value'>%s</span></div>"
             "<div class='section'>RTC (DST): <span class='value'>%s</span></div>"

             "%s%s%s"

             "<form id=\"clockForm\" method=\"POST\" action=\"/clock\">"
             "<input type=\"hidden\" name=\"line\" id=\"line\">"
             "<p id=\"preview\">Lokale Zeit wird ermittelt…</p>"
             "<input type=\"submit\" value=\"Uhr stellen\">"
             "</form>"

             "<p><a href=\"/\">Zurück</a></p>"
             "<p>%s</p>"

             "<script>"
             "const now = new Date();"
             "const weekday = ['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'][now.getDay()];"
             "const months = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];"
             "const day = now.getDate();"
             "const month = months[now.getMonth()];"
             "const year = now.getFullYear();"
             "const hour = now.getHours().toString().padStart(2,'0');"
             "const minute = now.getMinutes().toString().padStart(2,'0');"
             "const line = `${weekday}, ${day}. ${month} ${year}, ${hour}:${minute}`;"
             "document.getElementById('line').value = line;"
             "document.getElementById('preview').textContent = 'Lokale Zeit: ' + line;"
             "</script>"
             "</body></html>",
             current_raw,
             current_dst,
             (message && *message) ? "<div class='message'>" : "",
             (message && *message) ? message : "",
             (message && *message) ? "</div>" : "",
             timeout_info);

    debug_log("device settings page length: %d\n", strlen(page));
    send_response(tpcb, page);
}

void send_device_config_page(struct tcp_pcb* tpcb, const char* message) {
    char page[8192];
    char timeout_info[64];
    add_timeout_info(timeout_info, sizeof(timeout_info));

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head>"
             "<meta charset=\"UTF-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
             "<meta http-equiv=\"refresh\" content=\"300\">"
             "<title>Device Configuration</title>"
             "<style>"
             "body { font-family: sans-serif; text-align: center; }"
             "form { max-width: 400px; margin: auto; padding: 1em; }"
             "label { display: block; margin-bottom: 1em; font-size: 1em; text-align: left; }"
             "label.inline { display: inline-block; margin-right: 1em; }"
             "input[type='text'], input[type='number'] { width: 96%%; padding: 0.4em; font-size: 1em; box-sizing: border-box; }"
             "input[type='checkbox'], input[type='radio'] { width: auto; }"
             "input[type='submit'] { padding: 0.6em 1em; font-size: 1em; margin: 0.5em; width: 60%%; max-width: 200px; }"
             "fieldset { border: 1px solid #ccc; padding: 1em 1.2em; margin-top: 1em; text-align: left; }"
             "legend { font-weight: bold; }"
             ".message { margin: 1em auto; font-size: 1em; font-weight: bold; color: green; }"
             "</style></head><body>"
             "<h1>Device Configuration</h1>");

    if (message && *message) {
        snprintf(page + strlen(page), sizeof(page) - strlen(page),
                 "<div class='message'>%s</div>", message);
    }

    // Start of the form with general configuration grouped in a fieldset
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<form method=\"POST\" action=\"/device_config\">"
             "<fieldset><legend>Room Settings</legend>"

             // Room name input
             "<label>Room name:<br>"
             "<input type=\"text\" name=\"roomname\" value=\"%s\" maxlength=\"15\"></label>",
             device_config_flash.data.roomname);

    // Room type and number of seats
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<div style=\"margin-top:1em;\">"
             "<strong>Room type</strong><br>"
             "<label class=\"inline\"><input type=\"radio\" name=\"type\" value=\"0\" %s> Office</label>"
             "<label class=\"inline\"><input type=\"radio\" name=\"type\" value=\"1\" %s> Meeting</label>"
             "<label class=\"inline\"><input type=\"radio\" name=\"type\" value=\"2\" %s> Lecture hall</label>"
             "</div>"

             // Number of seats input
             "<label style=\"margin-top:1em; display:block;\">"
             "Number of seats:<br>"
             "<input type=\"number\" id=\"number_of_seats\" name=\"number_of_seats\" value=\"%d\" min=\"0\" max=\"5\">"
             "</label></fieldset>",
             (device_config_flash.data.type == 0 ? "checked" : ""),
             (device_config_flash.data.type == 1 ? "checked" : ""),
             (device_config_flash.data.type == 2 ? "checked" : ""),
             device_config_flash.data.number_of_seats);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<fieldset><legend>ePaper-Typ</legend>"
             "<label class=\"inline\"><input type=\"radio\" name=\"epapertype\" value=\"0\" %s onchange=\"updateSeatLimit()\"> None</label>"
             "<label class=\"inline\"><input type=\"radio\" name=\"epapertype\" value=\"1\" %s onchange=\"updateSeatLimit()\"> 7.5 Zoll</label>"
             "<label class=\"inline\"><input type=\"radio\" name=\"epapertype\" value=\"2\" %s onchange=\"updateSeatLimit()\"> 4.2 Zoll</label>"
             "</fieldset>",
             (device_config_flash.data.epapertype == 0 ? "checked" : ""),
             (device_config_flash.data.epapertype == 1 ? "checked" : ""),
             (device_config_flash.data.epapertype == 2 ? "checked" : ""));

    // Refresh-Intervalle
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<fieldset><legend>Refresh Intervals (minutes)</legend>");

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page (0): <input type=\"number\" name=\"refresh0\" value=\"%d\" min=\"1\" max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[0]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 1: <input type=\"number\" name=\"refresh1\" value=\"%d\" min=\"1\" max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[1]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 2: <input type=\"number\" name=\"refresh2\" value=\"%d\" min=\"1\" max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[2]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 3: <input type=\"number\" name=\"refresh3\" value=\"%d\" min=\"1\" max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[3]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 4: <input type=\"number\" name=\"refresh4\" value=\"%d\" min=\"1\" max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[4]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 5: <input type=\"number\" name=\"refresh5\" value=\"%d\" min=\"1\" max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[5]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 6: <input type=\"number\" name=\"refresh6\" value=\"%d\" min=\"1\" max=\"1440\"></label><br>",
             device_config_flash.data.refresh_minutes_by_pushbutton[6]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<label>Page 7: <input type=\"number\" name=\"refresh7\" value=\"%d\" min=\"1\" max=\"1440\"></label>",
             device_config_flash.data.refresh_minutes_by_pushbutton[7]);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page), "</fieldset>");

    // WiFi Settings section
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<fieldset><legend>WiFi Settings</legend>"

             // Number of WiFi attempts
             "<label>Number of WiFi Attempts:<br>"
             "<input type=\"number\" name=\"number_wifi_attempts\" value=\"%d\" min=\"1\" max=\"50\"></label>"

             "<label>WiFi Timeout (ms):<br>"
             "<input type=\"number\" name=\"wifi_timeout\" value=\"%d\" min=\"100\" max=\"10000\"></label>"

             "<label>Max Wait for Data (ms):<br>"
             "<input type=\"number\" name=\"max_wait_data_wifi\" value=\"%d\" min=\"10\" max=\"10000\"></label>"

             "<label>WiFi Reconnect Minutes:<br>"
             "<input type=\"number\" name=\"wifi_reconnect_minutes\" value=\"%d\" min=\"1\" max=\"30\"></label>"

             "</fieldset>",
             device_config_flash.data.number_wifi_attempts,
             device_config_flash.data.wifi_timeout,
             device_config_flash.data.max_wait_data_wifi,
             device_config_flash.data.wifi_reconnect_minutes);

    // Hardware settings section
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<fieldset><legend>Hardware</legend>"

             // Battery cutoff voltage
             "<label>Battery Cutoff Voltage (V):<br>"
             "<input type=\"number\" step=\"0.1\" name=\"switch_off_battery_voltage\" value=\"%.2f\" min=\"2.4\" max=\"3.9\"></label><br>"

             // Watchdog timeout
             "<label>Watchdog Timeout (ms):<br>"
             "<input type=\"number\" name=\"watchdog_time\" value=\"%d\" min=\"6000\" max=\"8000\"></label><br>"

             // Conversion factor
             "<label>Conversion Factor:<br>"
             "<input type=\"text\" name=\"conversion_factor\" value=\"%.6f\" step=\"any\"></label>"

             "</fieldset>",
             device_config_flash.data.switch_off_battery_voltage,
             device_config_flash.data.watchdog_time,
             device_config_flash.data.conversion_factor);

    // Checkboxes
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<div style=\"margin-top: 1em;\">"
             "<label class=\"inline\"><input type=\"checkbox\" name=\"show_query_date\" value=\"1\" %s> Show query timestamp</label><br>"
             "<label class=\"inline\"><input type=\"checkbox\" name=\"query_only_at_officehours\" value=\"1\" %s> Query only during office hours</label><br>"
             "</div>",
             (device_config_flash.data.show_query_date ? "checked" : ""),
             (device_config_flash.data.query_only_at_officehours ? "checked" : ""));

    // Submit, Footer
    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<input type=\"submit\" value=\"Store\">"
             "</form>"
             "<a href=\"/\">back</a>"
             "<p>%s</p>"
             "</body></html>",
             timeout_info);

    snprintf(strchr(page, '\0'), sizeof(page) - strlen(page),
             "<script>"
             "function updateSeatLimit() {"
             "  var epaper = document.querySelector('input[name=\"epapertype\"]:checked').value;"
             "  var seats = document.getElementById('number_of_seats');"
             "  if (epaper == '2') {"
             "    seats.value = 1;"
             "    seats.max = 1;"
             "  } else if (epaper == '1') {"
             "    if (seats.value > 3) seats.value = 3;"
             "    seats.max = 3;"
             "  } else {"
             "    seats.max = 5;"
             "  }"
             "}"
             "window.onload = updateSeatLimit;"
             "</script>");

    debug_log("device settings page length: %d\n", strlen(page));
    send_response(tpcb, page);
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

