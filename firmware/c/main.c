#include "boot_input.h"
#include "config.h"
#include "debug.h"
#include "epaper.h"
#include "flash.h"
#include "hardware/watchdog.h"
#include "http_client.h"
#include "i2c_bus.h"
#include "pico/stdlib.h"
#include "power.h"
#include "rtc.h"
#include "sensors.h"
#include "st25_io.h"
#include "use_case.h"
#include "webserver.h"
#include <stdint.h>
#include <stdio.h>

#if PICO_SDK_VERSION_MAJOR != 2 || PICO_SDK_VERSION_MINOR != 1 || PICO_SDK_VERSION_REVISION != 0
#warning                                                                                           \
    "This firmware was developed and tested with pico-sdk 2.1.0. Other versions may cause issues."
#endif

int main(void) {
    init_debug();

    set_debug_mode(DEBUG_BUFFERED);  // BUFFERED captures all boot logs (also before USB is ready);
                                     // flush before power down
    debug_set_verbosity(LOG_STATUS); // log boot health checks only

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
    rtc_init();

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

    // Setup mode entry via input_map
    if (page == PAGE_ACTION_SETUP) {
        debug_status("INFO", "%s: launching web interface (source=0x%X)\n", use_case.name,
                     input.source);
        webserver_run();
    }

    // Emergency mode: if no ePaper configured, go directly to WiFi setup
    if (device_config_flash.data.epapertype == EPAPER_NONE) {
        debug_status("WARN", "No ePaper configured — entering WiFi setup mode\n");
        webserver_run();
    }

    void *run_data = NULL;
    if (page >= 0 && use_case.pages[page] && use_case.pages[page]->needs_wifi && use_case.run) {
        run_data = use_case.run(battery_voltage, coin_cell_voltage);
    }

    // Auto-set RTC from server Date header
    rtc_sync_from_server();

    uint8_t *image_buf = epaper_init();
    if (image_buf == NULL) {
        debug_status("ERROR", "ePaper init or buffer allocation failed\n");
        debug_status("WARN", "Falling back to WiFi setup mode\n");
        webserver_run();
    }

    // Render: use case owns page dispatch and error handling
    use_case.render(image_buf, battery_voltage, page, run_data);

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
