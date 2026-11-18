#ifndef TRYTR_DOCUMENT_INTERNAL_H
#define TRYTR_DOCUMENT_INTERNAL_H

#include "trytr/document.h"

/* Closing root-brace offset, used to reject append frames that escape their body. */
size_t trytr_document_body_end(const TrytrDocument *document);

#endif
