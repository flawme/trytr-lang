#ifndef TRYTR_SRC_SVG_RENDER_H
#define TRYTR_SRC_SVG_RENDER_H

#include "trytr/document.h"

TrytrStatus trytr_svg_render(
    TrytrDocument *document,
    char **out_svg,
    size_t *out_length
);

/* Render vector content while leaving @math elements to a native host renderer. */
TrytrStatus trytr_svg_render_without_math(
    TrytrDocument *document,
    char **out_svg,
    size_t *out_length
);

#endif
