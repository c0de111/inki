#include "boot_input.h"
#define LOG_MODULE LOG_MOD_BOOT
#include "debug.h"
#include "flash.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "rtc.h"
#include "st25_io.h"
#include "use_case.h"
#include <stdio.h>
#include <string.h>

// Module state — read by telemetry and NFC renderers via extern
const char *wake_source = "unknown";
char nfc_text_buf[ST25_TEXT_MAX_LEN + 1] = "";
uint8_t nfc_image_buf[ST25_IMAGE_BYTES] = {0};
char nfc_booking_email[64] = "";
static char wake_source_buf[32];

static int read_pushbuttons(void) {
    int buttons = 0;

    // Read configured pushbuttons (active low with pull-up)
    if (device_config_flash.data.num_pushbuttons >= 1 &&
        device_config_flash.data.pushbutton1_pin != 0xFF) {
        gpio_init(device_config_flash.data.pushbutton1_pin);
        gpio_set_dir(device_config_flash.data.pushbutton1_pin, GPIO_IN);
        gpio_pull_up(device_config_flash.data.pushbutton1_pin);
        sleep_ms(5);
        if (!gpio_get(device_config_flash.data.pushbutton1_pin))
            buttons += 1;
    }

    if (device_config_flash.data.num_pushbuttons >= 2 &&
        device_config_flash.data.pushbutton2_pin != 0xFF) {
        gpio_init(device_config_flash.data.pushbutton2_pin);
        gpio_set_dir(device_config_flash.data.pushbutton2_pin, GPIO_IN);
        gpio_pull_up(device_config_flash.data.pushbutton2_pin);
        sleep_ms(5);
        if (!gpio_get(device_config_flash.data.pushbutton2_pin))
            buttons += 2;
    }

    if (device_config_flash.data.num_pushbuttons >= 3 &&
        device_config_flash.data.pushbutton3_pin != 0xFF) {
        gpio_init(device_config_flash.data.pushbutton3_pin);
        gpio_set_dir(device_config_flash.data.pushbutton3_pin, GPIO_IN);
        gpio_pull_up(device_config_flash.data.pushbutton3_pin);
        sleep_ms(5);
        if (!gpio_get(device_config_flash.data.pushbutton3_pin))
            buttons += 4;
    }

    return buttons;
}

// --- NFC reading + protocol handshake ---

static st25_boot_result_t read_nfc(void) {
    st25_boot_result_t st25 = st25_boot_check();

    if (st25.present && st25.request_valid) {
        // Delay lets phone finish RF readback before I2C writes block RF
        sleep_ms(200);
        st25_save_processed_nonce(st25.req.nonce);
        if (!st25_clear_request()) {
            dlog("[ST25] early clear failed!\n");
        }

        if (st25.req.opcode == ST25_OPCODE_TEXT) {
            st25_read_text(nfc_text_buf, sizeof(nfc_text_buf));
        } else if (st25.req.opcode == ST25_OPCODE_DRAW_IMAGE) {
            st25_read_image(nfc_image_buf, sizeof(nfc_image_buf));
        } else if (st25.req.opcode == ST25_OPCODE_BOOK_SEAT) {
            st25_read_text(nfc_booking_email, sizeof(nfc_booking_email));
        }
    }

    return st25;
}

// --- Unified boot input (classification) ---

boot_input_t read_boot_input(void) {
    boot_input_t result = {0};

    int buttons = read_pushbuttons();
    st25_boot_result_t st25 = read_nfc();
    result.st25_present = st25.present;

    if (st25.present) {
        debug_status("OK", "ST25: present (it_sts=0x%02X)\n", st25.it_sts);
    } else {
        debug_status("INFO", "ST25: not found\n");
    }

    // Wake source classification
    bool alarm_flag = rtc_alarm_flag_is_set();

    if (st25.present && st25.request_valid) {
        result.source = INPUT_NFC(st25.req.opcode);
        snprintf(wake_source_buf, sizeof(wake_source_buf), "nfc:0x%02X", st25.req.opcode);
        debug_status("OK", "Wake: NFC (opcode=0x%02X)\n", st25.req.opcode);
    } else if (alarm_flag) {
        result.source = INPUT_BUTTON(buttons);
        snprintf(wake_source_buf, sizeof(wake_source_buf), "rtc");
        debug_status("OK", "Wake: RTC ALARM\n");
    } else if (st25.present && st25.it_sts != 0 && buttons == 0) {
        result.reject = true;
        snprintf(wake_source_buf, sizeof(wake_source_buf), "spurious");
        debug_status("WARN", "Rejecting boot: spurious NFC (it_sts=0x%02X)\n", st25.it_sts);
    } else {
        result.source = INPUT_BUTTON(buttons);
        snprintf(wake_source_buf, sizeof(wake_source_buf), "button:%d", buttons);
        debug_status("OK", "Wake: BUTTON (buttons=%d)\n", buttons);
    }

    wake_source = wake_source_buf;
    return result;
}
