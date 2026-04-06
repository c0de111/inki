#ifndef EPAPER_LAYOUT_H
#define EPAPER_LAYOUT_H

#include "DEV_Config.h"
#include "ImageResources.h"
#include "fonts.h"

typedef enum {
    ELEM_TEXT,     // static string at absolute position
    ELEM_VAR,      // runtime string from vars[] array
    ELEM_LOGO,     // logo with fallback image (try custom flash logo first)
    ELEM_SUBIMAGE, // static bitmap at absolute position
    ELEM_VLINE,    // vertical line from (x, y) to (x, end_coord)
} elem_type_t;

typedef struct {
    elem_type_t type;
    int x, y;
    const sFONT *font; // for ELEM_TEXT / ELEM_VAR
    union {
        const char *text;      // ELEM_TEXT: string literal
        int var_index;         // ELEM_VAR: index into vars[]
        const SubImage *image; // ELEM_SUBIMAGE / ELEM_LOGO fallback
        int end_coord;         // ELEM_VLINE: y end coordinate
    };
} layout_elem_t;

#define LAYOUT_MAX_VARS 12

void epaper_render_layout(uint8_t *buffer, const layout_elem_t *elements, int count,
                          const char *vars[]);

#endif
