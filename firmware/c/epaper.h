#ifndef EPAPER_H
#define EPAPER_H

#include <stdbool.h>
#include <stdint.h>

uint8_t *epaper_init(bool is_4gray);
void epaper_flush_and_sleep(uint8_t *image);

// Capability accessors — valid after epaper_init() returns non-NULL.
uint16_t epaper_get_width(void);
uint16_t epaper_get_height(void);
bool epaper_is_4gray(void);

#endif
