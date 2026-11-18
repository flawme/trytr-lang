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

/* One validated math element, for embedding at its position in a document.
 * The returned standalone <math display="block"> string must be free()d. */
TrytrStatus trytr_mathml_render_element(
    const TrytrDocumentElement *element, char **out_mathml, size_t *out_length);

/* Render one document-owned inline math run as <math display="inline">.
 * Tree spans are relative to the run's content slice. Caller frees output. */
TrytrStatus trytr_mathml_render_inline_run(
    const TrytrDocumentElement *element, const TrytrInlineRun *run,
    char **out_mathml, size_t *out_length);

#endif
