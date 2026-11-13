#ifndef TRYTR_SRC_TEXT_RENDER_H
#define TRYTR_SRC_TEXT_RENDER_H

#include "trytr/document.h"

TrytrStatus trytr_text_render(
    TrytrDocument *document,
    char **out_text,
    size_t *out_length
);

#endif
