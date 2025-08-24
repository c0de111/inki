#include "webserver_utils.h"
#include "flash.h"
#include "hardware/flash.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

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

void safe_flash_copy(void* dest, const void* flash_src, size_t len) {
    const volatile uint8_t* src = (const volatile uint8_t*)flash_src;
    uint8_t* dst = (uint8_t*)dest;
    for (size_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

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

void parse_form_fields(const char *body, int len, web_submission_t *result) {
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