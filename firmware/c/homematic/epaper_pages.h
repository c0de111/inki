#ifndef HOMEMATIC_EPAPER_PAGES_H
#define HOMEMATIC_EPAPER_PAGES_H

#include "DEV_Config.h"

void render_page_homematic(uint8_t *image_buffer, float battery_voltage);
void render_page_homematic_placeholder(uint8_t *image_buffer, float battery_voltage);

#endif // HOMEMATIC_EPAPER_PAGES_H
