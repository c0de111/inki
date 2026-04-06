#ifndef EPAPER_H
#define EPAPER_H

#include <stdint.h>

uint8_t *epaper_init(void);
void epaper_flush_and_sleep(uint8_t *image);

#endif
