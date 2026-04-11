#include "epaper.h"
#include "DEV_Config.h"
#include "EPD_2in9_V2.h"
#include "EPD_4in2_V2.h"
#include "EPD_7in5_V2.h"
#include "GUI_Paint.h"
#include "config.h"
#define LOG_MODULE LOG_MOD_EPAPER
#include "debug.h"
#include "flash.h"
#include "hardware/watchdog.h"
#include <stdlib.h>

uint8_t *epaper_init(void) {

    if (device_config_flash.data.epapertype == EPAPER_NONE) {
        dlog("No ePaper configured for this room.\n");
        return NULL;
    }

    watchdog_update();

    // Initialize the hardware module for the ePaper
    if (DEV_Module_Init() != 0) {
        dlog("Error initializing ePaper hardware module.\n");
        return NULL;
    }

    dtrace("Disabling watchdog for ePaper setup...\n");
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);

    UDOUBLE buffer_size = 0;

    // Initialize and clear the ePaper based on the configured type
    switch (device_config_flash.data.epapertype) {
    case EPAPER_WAVESHARE_7IN5_V2:
        dlog("Initializing Waveshare 7.5-inch V2 ePaper...\n");
#ifdef USE_CASE_WEATHERMAP
        EPD_7IN5_V2_Init_4Gray();
        EPD_7IN5_V2_Clear(); // Clear is needed to reset display content
        buffer_size =
            ((EPD_7IN5_V2_WIDTH % 8 == 0) ? (EPD_7IN5_V2_WIDTH / 4) : (EPD_7IN5_V2_WIDTH / 4 + 1)) *
            EPD_7IN5_V2_HEIGHT; // 4Gray: 2 bits per pixel
#else
        EPD_7IN5_V2_Init();
        EPD_7IN5_V2_Clear();
        buffer_size =
            ((EPD_7IN5_V2_WIDTH % 8 == 0) ? (EPD_7IN5_V2_WIDTH / 8) : (EPD_7IN5_V2_WIDTH / 8 + 1)) *
            EPD_7IN5_V2_HEIGHT;
#endif
        break;

    case EPAPER_WAVESHARE_4IN2_V2:
        dlog("Initializing Waveshare 4.2-inch ePaper...\n");
#ifdef USE_CASE_WEATHERMAP
        // Waveshare official pattern: First clear in regular mode, then switch to 4Gray
        EPD_4IN2_V2_Init();
        EPD_4IN2_V2_Clear();
        sleep_ms(500); // Official timing from Waveshare examples
        EPD_4IN2_V2_Init_4Gray();
        buffer_size =
            ((EPD_4IN2_V2_WIDTH % 8 == 0) ? (EPD_4IN2_V2_WIDTH / 4) : (EPD_4IN2_V2_WIDTH / 4 + 1)) *
            EPD_4IN2_V2_HEIGHT; // 4Gray: 2 bits per pixel
#else
        EPD_4IN2_V2_Init();
        EPD_4IN2_V2_Clear();
        buffer_size =
            ((EPD_4IN2_V2_WIDTH % 8 == 0) ? (EPD_4IN2_V2_WIDTH / 8) : (EPD_4IN2_V2_WIDTH / 8 + 1)) *
            EPD_4IN2_V2_HEIGHT;
#endif
        break;

    case EPAPER_WAVESHARE_2IN9_V2:
        dlog("Initializing Waveshare 2.9-inch V2 ePaper...\n");
        EPD_2IN9_V2_Init();
        EPD_2IN9_V2_Clear();
        buffer_size =
            ((EPD_2IN9_V2_WIDTH % 8 == 0) ? (EPD_2IN9_V2_WIDTH / 8) : (EPD_2IN9_V2_WIDTH / 8 + 1)) *
            EPD_2IN9_V2_HEIGHT;
        break;

    default:
        dlog("Unsupported ePaper type: %d\n", device_config_flash.data.epapertype);
        hw_set_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS); // Re-enable watchdog
        return NULL;
    }

    dtrace("Re-enabling watchdog...\n");
    watchdog_enable(device_config_flash.data.watchdog_time, 0);
    watchdog_update();

    // Create a new image cache
    uint8_t *image_buf = (uint8_t *)malloc(buffer_size);
    if (image_buf == NULL) {
        debug_status("ERROR", "ePaper: buffer allocation failed (%lu bytes)\n",
                     (unsigned long)buffer_size);
        hw_set_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
        return NULL;
    }

    Paint_NewImage(
        image_buf,
        (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) ? EPD_7IN5_V2_WIDTH
                                                                          : EPD_4IN2_V2_WIDTH,
        (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) ? EPD_7IN5_V2_HEIGHT
                                                                          : EPD_4IN2_V2_HEIGHT,
        0, WHITE);

    Paint_SelectImage(image_buf);
#ifdef USE_CASE_WEATHERMAP
    Paint_SetScale(4);  // Enable 4Gray mode for weathermap
    Paint_Clear(GRAY4); // Clear to white background
#else
    Paint_Clear(WHITE);
#endif

    watchdog_update();

    const char *type_name =
        (device_config_flash.data.epapertype == EPAPER_WAVESHARE_7IN5_V2) ? "7.5\" V2" : "4.2\" V2";
    debug_status("OK", "ePaper: %s, %lu bytes\n", type_name, (unsigned long)buffer_size);
    return image_buf;
}

void epaper_flush_and_sleep(uint8_t *image) {
    if (image == NULL) {
        dlog("No valid image buffer to display. Skipping ePaper operations.\n");
        return;
    }

    watchdog_update();

    dtrace("EPD_Display called for epaper type: %d\n", device_config_flash.data.epapertype);
    switch (device_config_flash.data.epapertype) {
    case EPAPER_WAVESHARE_7IN5_V2:
#ifdef USE_CASE_WEATHERMAP
        EPD_7IN5_V2_Display_4Gray(image);
#else
        EPD_7IN5_V2_Display(image);
#endif
        break;

    case EPAPER_WAVESHARE_4IN2_V2:
#ifdef USE_CASE_WEATHERMAP
        EPD_4IN2_V2_Display_4Gray(image);
#else
        EPD_4IN2_V2_Display(image);
#endif
        break;

    case EPAPER_WAVESHARE_2IN9_V2:
        EPD_2IN9_V2_Display(image);
        break;

    default:
        dlog("Unsupported ePaper type: %d\n", device_config_flash.data.epapertype);
        free(image);
        return;
    }

    // Free allocated memory for the image
    free(image);
    watchdog_update();

    dtrace("Entering ePaper sleep mode for type: %d\n", device_config_flash.data.epapertype);
    switch (device_config_flash.data.epapertype) {
    case EPAPER_WAVESHARE_7IN5_V2:
        EPD_7IN5_V2_Sleep();
        break;

    case EPAPER_WAVESHARE_4IN2_V2:
        EPD_4IN2_V2_Sleep();
        break;

    case EPAPER_WAVESHARE_2IN9_V2:
        EPD_2IN9_V2_Sleep();
        break;

    default:
        dlog("Unsupported ePaper type during sleep: %d\n", device_config_flash.data.epapertype);
        return;
    }

    // Short delay to ensure the sleep command is processed
    DEV_Delay_ms(200);

    dtrace("Shutting down the ePaper module...\n");
    DEV_Module_Exit();
    watchdog_update();
}
