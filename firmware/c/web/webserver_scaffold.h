#pragma once

#include <stddef.h>

#define SCAFFOLD_PAGE_SIZE 6144

// Unified page scaffold for all inki setup pages.
// Uses the device-status CSS as the canonical style (responsive, card-based).

// Write HTML document opening: DOCTYPE, head, unified CSS, body/container, <h1>.
// auto_refresh_s: 0 = no refresh, >0 = meta http-equiv refresh in seconds.
// Returns bytes written (suitable for offset tracking: n = scaffold_page_open(...)).
int scaffold_page_open(char *buf, size_t sz, const char *title, int auto_refresh_s);

// Write closing tags: optional back link, optional footer, </div></body></html>.
// back_href: NULL or "" to skip. footer_html: NULL or "" to skip.
int scaffold_page_close(char *buf, size_t sz, const char *back_href, const char *footer_html);
