#ifndef TRYTR_DOCUMENT_H
#define TRYTR_DOCUMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "trytr/math/notation.h"

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

typedef struct TrytrParameterOverride {
    const char *id;
    int64_t value;
} TrytrParameterOverride;

typedef struct TrytrDocumentParameter {
    const char *id;
    const char *label;
    size_t label_length;
    int64_t minimum;
    int64_t maximum;
    int64_t default_value;
    int64_t value;
    int64_t step;
    TrytrSourceLocation location;
} TrytrDocumentParameter;

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
    TRYTR_ELEMENT_ELLIPSE,
    TRYTR_ELEMENT_POLYGON,
    TRYTR_ELEMENT_ROUNDRECT,
    TRYTR_ELEMENT_PLOT
} TrytrElementKind;

typedef enum TrytrTextAlignment {
    TRYTR_TEXT_ALIGN_LEFT = 0,
    TRYTR_TEXT_ALIGN_CENTER,
    TRYTR_TEXT_ALIGN_RIGHT
} TrytrTextAlignment;

/*
 * A read-only view of one validated element. The view struct is valid only
 * during the visitor call; its id and text pointers remain valid until the
 * document is destroyed. text is set for heading, text, math, path, polygon,
 * and plot elements. Path text contains SVG commands; polygon text contains
 * validated coordinate pairs; plot text contains validated data coordinates.
 * Rectangles and rounded rectangles use x, y, width, and height; rounded
 * rectangles also use radius for all four corners. Lines use x, y, x2, and y2;
 * circles use x, y, and radius. Ellipses use x, y, radius_x, and radius_y. Group begin
 * events use x and y as local translations; matching group end events close
 * the scope. Group events are included in
 * source traversal order. Paths use min_x, min_y, max_x, and max_y for
 * conservative bounds. Plots use x, y, width, and height for the chart box;
 * point_count describes validated plot data. min_x/min_y/max_x/max_y retain
 * conservative integer bounds; plot_min_x/plot_min_y/plot_max_x/plot_max_y
 * preserve the precise decimal data bounds. formula_plot identifies plots
 * sampled from a math expression. Only id
 * is an author-defined persistent identity.
 * Optional fill_rgb and stroke_rgb values are packed as 0xRRGGBB; check their
 * corresponding has_fill and has_stroke flags before reading them.
 * Optional stroke_width is measured in document units and uses
 * has_stroke_width to distinguish an explicit value from the default of 1
 * (plots default to 2). Plots set plot_grid and plot_markers to their effective
 * values; both default to true. These fields are false for non-plot elements.
 * Text elements may set font_size, text_color_rgb, and text_alignment; check
 * each corresponding has_* flag before reading an optional value.
 * Heading and text elements may set text_width as a wrapping width in
 * document units; math elements keep their source lines unwrapped.
 * Math elements also expose an immutable math_tree. Its nodes and source spans
 * remain valid until the document is destroyed; node offsets refer to text.
 */
typedef struct TrytrDocumentElement {
    TrytrElementKind kind;
    size_t element_index;
    const char *id;
    const char *text;
    size_t text_length;
    const TrytrMathNode *math_tree;
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
    double plot_min_x;
    double plot_min_y;
    double plot_max_x;
    double plot_max_y;
    size_t point_count;
    bool formula_plot;
    TrytrSourceLocation location;
    bool has_fill;
    uint32_t fill_rgb;
    bool has_stroke;
    uint32_t stroke_rgb;
    bool has_stroke_width;
    int64_t stroke_width;
    bool has_font_size;
    int64_t font_size;
    bool has_text_color;
    uint32_t text_color_rgb;
    bool has_text_alignment;
    TrytrTextAlignment text_alignment;
    bool has_text_width;
    int64_t text_width;
    /* Plot controls resolve to true by default; stroke style is optional. */
    bool plot_grid;
    bool plot_markers;
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

/*
 * Parses a document with explicit values for declared parameters. Overrides
 * are data, never source text. Each supplied ID must be declared exactly once
 * and each value must lie on that parameter's bounded step interval.
 */
TrytrStatus trytr_document_parse_with_parameters(
    const char *source,
    size_t length,
    const TrytrParameterOverride *overrides,
    size_t override_count,
    TrytrDocument **out_document
);

size_t trytr_document_parameter_count(const TrytrDocument *document);
/* The returned descriptor and its strings live until the document is destroyed. */
const TrytrDocumentParameter *trytr_document_parameter_at(
    const TrytrDocument *document,
    size_t index
);

TrytrStatus trytr_document_validate(TrytrDocument *document);
/* Returns true and writes both dimensions when the document has a fixed canvas. */
bool trytr_document_get_canvas_size(
    const TrytrDocument *document,
    int64_t *out_width,
    int64_t *out_height
);
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
