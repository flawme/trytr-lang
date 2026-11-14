#ifndef TRYTR_PDF_RENDER_H
#define TRYTR_PDF_RENDER_H

#include "trytr/document.h"

#include <stdbool.h>
#include <stddef.h>

bool trytr_pdf_export_available(void);
TrytrStatus trytr_pdf_render_svg(
    const char *svg,
    size_t svg_length,
    char **out_pdf,
    size_t *out_length
);

#endif
