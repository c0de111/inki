#ifndef WEBSERVER_FLASH_H
#define WEBSERVER_FLASH_H

#include <stdint.h>
#include <stddef.h>

void mark_firmware_valid(uint32_t flash_offset);

void flush_page_to_flash(void);

#endif // WEBSERVER_FLASH_H