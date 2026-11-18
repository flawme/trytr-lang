#ifndef TRYTR_LAYOUT_H
#define TRYTR_LAYOUT_H

#include "trytr/document.h"

typedef struct TrytrLayout TrytrLayout;

typedef enum TrytrLayoutBlockKind {
    TRYTR_LAYOUT_HEADING,
    TRYTR_LAYOUT_TEXT,
    TRYTR_LAYOUT_MATH,
    TRYTR_LAYOUT_CANVAS
} TrytrLayoutBlockKind;

typedef struct TrytrLayoutOptions {
    /* 0 preserves author-only wrapping; otherwise 1..1,000,000 units. */
    int64_t width;
    /* Space between blocks, 0..1,000,000 units. NULL options default to 8. */
    int64_t gap;
} TrytrLayoutOptions;

typedef struct TrytrLayoutElement {
    TrytrDocumentElement element;
    /* Composed group translation; element geometry stays in local coordinates. */
    int64_t translate_x;
    int64_t translate_y;
} TrytrLayoutElement;

typedef struct TrytrLayoutLine {
    /* Byte span in this block's sole text element. Never splits a UTF-8 scalar
     * or an inline math run. Widths estimate the raw source, including fences. */
    size_t start;
    size_t length;
    int64_t width;
} TrytrLayoutLine;

typedef struct TrytrLayoutBlock {
    TrytrLayoutBlockKind kind;
    int64_t x;
    int64_t y;
    int64_t width;
    int64_t height;
    int64_t font_size;
    int64_t line_height;
    size_t first_element;
    size_t element_count;
    size_t first_line;
    size_t line_count;
    /* Canvas viewport, in document coordinates before flow placement. */
    int64_t canvas_x;
    int64_t canvas_y;
} TrytrLayoutBlock;

/*
 * Create an immutable source-order flow snapshot. Contiguous drawing elements
 * form one canvas block; text and equations split canvas runs. Geometry inside
 * a run retains its coordinates and composed group translations. A fixed
 * document canvas clips each run to that viewport.
 *
 * The document must outlive the snapshot (which borrows its text/math trees).
 * Line widths use conservative Unicode-scalar metrics, not font shaping.
 * Math metrics (including inline formulas) estimate source text; a native host
 * must measure actual line heights and reflow later blocks. No pagination yet.
 * Limits: 4,096 elements/blocks, 65,536 lines, 1,000,000,000-unit extents.
 * On any failure out_layout is NULL; no partial snapshot is published.
 */
TrytrStatus trytr_layout_create(TrytrDocument *document,
                                const TrytrLayoutOptions *options,
                                TrytrLayout **out_layout);
void trytr_layout_destroy(TrytrLayout *layout);
size_t trytr_layout_block_count(const TrytrLayout *layout);
const TrytrLayoutBlock *trytr_layout_block_at(const TrytrLayout *layout, size_t index);
const TrytrLayoutElement *trytr_layout_element_at(const TrytrLayout *layout, size_t index);
const TrytrLayoutLine *trytr_layout_line_at(const TrytrLayout *layout, size_t index);

#endif
