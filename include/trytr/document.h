#ifndef TRYTR_DOCUMENT_H
#define TRYTR_DOCUMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct TrytrDocument TrytrDocument;

typedef enum TrytrStatus {
    TRYTR_STATUS_OK = 0,
    TRYTR_STATUS_INVALID_ARGUMENT,
    TRYTR_STATUS_INVALID_DOCUMENT,
    TRYTR_STATUS_NO_MEMORY,
    TRYTR_STATUS_LIMIT_EXCEEDED,
    TRYTR_STATUS_EXECUTION_FAILED
} TrytrStatus;

typedef enum TrytrDiagnosticSeverity {
    TRYTR_DIAGNOSTIC_INFO = 0,
    TRYTR_DIAGNOSTIC_WARNING,
    TRYTR_DIAGNOSTIC_ERROR,
    TRYTR_DIAGNOSTIC_FATAL
} TrytrDiagnosticSeverity;

typedef struct TrytrSourceLocation {
    size_t offset;
    size_t line;
    size_t column;
} TrytrSourceLocation;

typedef enum TrytrElementKind {
    TRYTR_ELEMENT_HEADING = 0,
    TRYTR_ELEMENT_TEXT,
    TRYTR_ELEMENT_MATH,
    TRYTR_ELEMENT_RECT,
    TRYTR_ELEMENT_LINE,
    TRYTR_ELEMENT_CIRCLE,
    TRYTR_ELEMENT_PATH,
    TRYTR_ELEMENT_GROUP_BEGIN,
    TRYTR_ELEMENT_GROUP_END,
    TRYTR_ELEMENT_ELLIPSE
} TrytrElementKind;

/*
 * A read-only view of one validated element. The view struct is valid only
 * during the visitor call; its id and text pointers remain valid until the
 * document is destroyed. text is set for heading, text, math, and path elements
 * (path text contains validated SVG path commands).
 * Rectangles use x, y, width, and height; lines use x, y, x2, and y2; circles
 * use x, y, and radius. Ellipses use x, y, radius_x, and radius_y. Group begin
 * events use x and y as local translations; matching group end events close
 * the scope. Group events are included in
 * source traversal order. Paths use min_x, min_y, max_x, and max_y for
 * conservative bounds. Only id is an author-defined persistent identity.
 */
typedef struct TrytrDocumentElement {
    TrytrElementKind kind;
    size_t element_index;
    const char *id;
    const char *text;
    size_t text_length;
    int64_t x;
    int64_t y;
    int64_t width;
    int64_t height;
    int64_t x2;
    int64_t y2;
    int64_t radius;
    int64_t radius_x;
    int64_t radius_y;
    int64_t min_x;
    int64_t min_y;
    int64_t max_x;
    int64_t max_y;
    TrytrSourceLocation location;
} TrytrDocumentElement;

/* Return false to stop execution; the API then returns EXECUTION_FAILED. */
typedef bool (*TrytrElementVisitor)(
    const TrytrDocumentElement *element,
    void *user_data
);

typedef struct TrytrDiagnostic {
    /* Pointers returned from a document remain valid until that document is destroyed. */
    const char *code;
    TrytrDiagnosticSeverity severity;
    TrytrSourceLocation location;
    const char *message;
} TrytrDiagnostic;

const char *trytr_status_string(TrytrStatus status);

/*
 * Parses source_length bytes. For valid arguments, out_document receives an owned
 * handle even when parsing reports an invalid document, so callers can inspect
 * diagnostics and must release the handle with trytr_document_destroy().
 */
TrytrStatus trytr_document_parse(
    const char *source,
    size_t length,
    TrytrDocument **out_document
);

TrytrStatus trytr_document_validate(TrytrDocument *document);
/*
 * Executes the validated, bounded document IR in source order. The visitor
 * must not destroy this document or recursively execute it.
 */
TrytrStatus trytr_document_execute(
    TrytrDocument *document,
    TrytrElementVisitor visitor,
    void *user_data
);
/* Releases the document and all diagnostics returned from it. */
void trytr_document_destroy(TrytrDocument *document);

size_t trytr_document_diagnostic_count(const TrytrDocument *document);
const TrytrDiagnostic *trytr_document_diagnostic_at(
    const TrytrDocument *document,
    size_t index
);

#endif
