#include "boot_input.h"
#include "config.h"
#include "debug.h"
#include "device_caps.h"
#include "epaper.h"
#include "epaper_render.h"
#include "flash.h"
#include "hardware/watchdog.h"
#include "http_client.h"
#include "i2c_bus.h"
#include "inki_monitor.h"
#include "pico/stdlib.h"
#include "power.h"
#include "rtc.h"
#include "sensors.h"
#include "st25_io.h"
#include "use_case.h"
#include "webserver.h"
#include "wifi.h"
#include <stdint.h>
#include <stdio.h>

#if PICO_SDK_VERSION_MAJOR != 2 || PICO_SDK_VERSION_MINOR != 1 || PICO_SDK_VERSION_REVISION != 0
#warning                                                                                           \
    "This firmware was developed and tested with pico-sdk 2.1.0. Other versions may cause issues."
#endif

static device_caps_t build_device_caps(const i2c_probe_result_t *probe) {
    uint8_t strap = read_strap_pins();
    return (device_caps_t){
        .epaper_width = epaper_get_width(),
        .epaper_height = epaper_get_height(),
        .epaper_4gray = epaper_is_4gray(),
        .st25_present = probe->st25_user_present,
        .bmp581_present = probe->bmp581_present,
        .ds3231_present = probe->ds3231_present,
        .rv3028_present = probe->rv3028_present,
        .coin_cell_present = (strap != 0x05),
    };
}

int main(void) {
    init_debug();

    set_debug_mode(DEBUG_BUFFERED); // BUFFERED captures all boot logs (also before USB is ready);
                                    // flush before power down

    // --- Verbosity configuration ---
    // General   :        debug_set_verbosity(LOG_STATUS);
    //                    → STATUS from all modules, DEBUG/TRACE suppressed globally
    //
    // Dev (one module):  debug_set_verbosity(LOG_TRACE);
    //                    debug_set_module_mask(LOG_MOD_ST25);
    //                    → TRACE from ST25, STATUS+INFO from all, DEBUG/TRACE from others
    //                    suppressed
    //
    // Dev (multi):       debug_set_verbosity(LOG_TRACE);
    //                    debug_set_module_mask(LOG_MOD_HTTP | LOG_MOD_TLS);
    //
    // Note: STATUS and INFO always pass regardless of module mask (they are module-agnostic).
    debug_set_verbosity(LOG_STATUS);

    power_hold();
    debug_status("OK", "System initializing - inki-%s\n", use_case.name);

    stdio_init_all();

    http_client_init();

#if INKI_DEBUG_USB_WAIT
    if (debug_wait_for_usb(2500)) {
        printf("USB connected\n");
    } else {
        printf("USB timeout\n");
    }
#endif

    flash_log_status();

    watchdog_enable(device_config_flash.data.watchdog_time, 0);
    debug_status("OK", "Watchdog enabled (%d ms)\n", device_config_flash.data.watchdog_time);

    float battery_voltage = read_battery_voltage(device_config_flash.data.conversion_factor);
    float coin_cell_voltage = read_coin_cell_voltage(device_config_flash.data.conversion_factor);
    debug_status(battery_voltage < 3.4f ? "WARN" : "OK", "Battery: %.2fV\n", battery_voltage);

    i2c_bus_init();
    i2c_probe_result_t probe = {0};
    i2c_probe_expected_devices(&probe);
    rtc_init(&probe);

    // Read all boot-time input sources (pushbuttons + NFC + RTC alarm)
    boot_input_t input = read_boot_input();
    // Spurious NFC wake — shut down immediately
    if (input.reject) {
        debug_flush();
        power_shutdown_spurious(input.st25_present);
    }

    int page = resolve_page_index(input.source);
    bool wifi_needed = page >= 0 && use_case.pages[page] && use_case.pages[page]->needs_wifi;
    debug_status("OK", "Page: %d (wifi=%s)\n", page, wifi_needed ? "yes" : "no");

    // Emergency mode: unconfigured device → treat as setup
    if (device_config_flash.data.epapertype == EPAPER_NONE) {
        debug_status("WARN", "No ePaper configured — entering WiFi setup mode\n");
        page = PAGE_ACTION_SETUP;
    }

    if (page == PAGE_ACTION_SETUP) {
        debug_status("INFO", "%s: launching web interface (source=0x%X)\n", use_case.name,
                     input.source);
        webserver_run();
        // After setup (save+exit or timeout): run page 0 as a normal RTC wake cycle.
        page = 0;
        wifi_needed = use_case.pages[page] && use_case.pages[page]->needs_wifi;
    }

    void *run_data = NULL;
    int render_page = page;
    if (wifi_needed) {
        if (wifi_connect() != WIFI_SUCCESS) {
            render_page = PAGE_WIFI_ERROR;
        } else {
            if (use_case.run) {
                run_result_t result = use_case.run();
                run_data = result.data;
                if (!run_data && result.error_page)
                    render_page = result.error_page;
            }
            rtc_sync_from_server();
            inki_monitor_send_telemetry(battery_voltage, coin_cell_voltage, run_data != NULL);
            wifi_log_rssi();
            wifi_deinit();
        }
    }

    epaper_set_battery_voltage(battery_voltage);

    uint8_t *image_buf = epaper_init(use_case.needs_4gray);
    if (image_buf == NULL) {
        debug_status("ERROR", "ePaper init or buffer allocation failed\n");
        debug_status("WARN", "Falling back to WiFi setup mode\n");
        webserver_run();
    }

    device_caps_t caps = build_device_caps(&probe);
    use_case.render(image_buf, caps, render_page, run_data);

    if (use_case.free_data)
        use_case.free_data(run_data);

    epaper_flush_and_sleep(image_buf);

    // ST25 cleanup: clear any request written during ePaper cycle, then power off
    if (input.st25_present) {
        st25_clear_request();
        st25_power_off();
    }

    rtc_set_alarm(page);

    debug_status("OK", "System shutting down\n");
    debug_flush();

    power_off();
}
