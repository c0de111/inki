#ifndef HISTORIAN_EPAPER_PAGES_H
#define HISTORIAN_EPAPER_PAGES_H

#include "DEV_Config.h"

void render_page_historian(uint8_t *image_buffer, float battery_voltage);
void render_page_historian_placeholder(uint8_t *image_buffer, float battery_voltage);

#endif // HISTORIAN_EPAPER_PAGES_H
