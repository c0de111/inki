#pragma once

#include "third_party/GUI/GUI_Paint.h"

// Weathermap page 0: render stored map from flash, fallback to test pattern.
// Signature matches page_renderer_t (uint8_t *image_buffer).
void render_page_weathermap(uint8_t *image_buffer);
