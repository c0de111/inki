#include "weathermap/epaper_pages.h"

#include "debug.h"
#include "epaper_render.h"
#include "fonts.h"
#include "third_party/GUI/GUI_Paint.h"
#include "weathermap/client.h"

void render_page_weathermap(uint8_t *image_buffer, float battery_voltage) {
    (void)image_buffer;

    if (!weathermap_render_from_flash()) {
        epaper_draw_4gray_test();
        Paint_DrawString_EN(10, 10, "No map in flash", &font_ubuntu_mono_8pt_bold, WHITE, BLACK);
    }
    epaper_draw_firmware_info(battery_voltage);
}
