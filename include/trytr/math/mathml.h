#ifndef TRYTR_MATH_MATHML_H
#define TRYTR_MATH_MATHML_H

#include <stddef.h>

#include "trytr/document.h"

/*
 * Render the document's @math elements as a standalone MathML Core tree.
 * The returned UTF-8 string is allocated by Trytr and must be freed with
 * free(). Documents without @math elements produce an empty string.
 */
TrytrStatus trytr_mathml_render(
    TrytrDocument *document,
    char **out_mathml,
    size_t *out_length
);

#endif
