#ifndef TRYTR_LAYOUT_GEOMETRY_H
#define TRYTR_LAYOUT_GEOMETRY_H

#include "trytr/document.h"

typedef struct TrytrLayoutBounds {
    int64_t min_x;
    int64_t min_y;
    int64_t max_x;
    int64_t max_y;
} TrytrLayoutBounds;

bool trytr_layout_geometry_bounds(const TrytrDocumentElement *element,
                                   int64_t translate_x, int64_t translate_y,
                                   TrytrLayoutBounds *out_bounds);

#endif
