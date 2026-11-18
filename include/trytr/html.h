#ifndef TRYTR_HTML_H
#define TRYTR_HTML_H

#include "trytr/document.h"

/* Render an offline HTML document in source order: native text and MathML,
 * with SVG only for canvas blocks. The browser measures native equations and
 * reflows following blocks. Caller owns the UTF-8 buffer and must free() it. */
TrytrStatus trytr_html_render(TrytrDocument *document, char **out_html, size_t *out_length);

#endif
