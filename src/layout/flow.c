#include "trytr/layout.h"

#include "geometry.h"
#include "text.h"
#include "../limits.h"

#include <stdlib.h>

#define LAYOUT_LINE_LIMIT 65536u
#define LAYOUT_EXTENT_LIMIT INT64_C(1000000000)

struct TrytrLayout {
    TrytrLayoutBlock *blocks;
    TrytrLayoutElement *elements;
    TrytrLayoutLine *lines;
    size_t block_count, block_capacity;
    size_t element_count, element_capacity;
    size_t line_count, line_capacity;
};

typedef struct FlowState {
    TrytrLayout *layout;
    TrytrLayoutOptions options;
    TrytrStatus status;
    int64_t tx, ty;
    int64_t stack_x[TRYTR_DEFAULT_DEPTH_LIMIT];
    int64_t stack_y[TRYTR_DEFAULT_DEPTH_LIMIT];
    size_t depth;
    bool fixed_canvas;
    int64_t canvas_width, canvas_height;
} FlowState;

static void *grow(void *data, size_t count, size_t *capacity,
                   size_t item_size, size_t limit, TrytrStatus *status)
{
    if (count >= limit) { *status = TRYTR_STATUS_LIMIT_EXCEEDED; return NULL; }
    if (count < *capacity) return data;
    size_t next = *capacity == 0u ? 16u : *capacity * 2u;
    if (next > limit) next = limit;
    void *result = realloc(data, next * item_size);
    if (result == NULL) { *status = TRYTR_STATUS_NO_MEMORY; return NULL; }
    *capacity = next;
    return result;
}

static bool add_element(FlowState *state, const TrytrDocumentElement *element)
{
    TrytrLayout *layout = state->layout;
    void *items = grow(layout->elements, layout->element_count, &layout->element_capacity,
                       sizeof(*layout->elements), TRYTR_DEFAULT_NODE_LIMIT, &state->status);
    if (items == NULL) return false;
    layout->elements = items;
    layout->elements[layout->element_count++] = (TrytrLayoutElement){
        .element = *element, .translate_x = state->tx, .translate_y = state->ty};
    return true;
}

static TrytrLayoutBlock *add_block(FlowState *state, TrytrLayoutBlockKind kind)
{
    TrytrLayout *layout = state->layout;
    void *items = grow(layout->blocks, layout->block_count, &layout->block_capacity,
                       sizeof(*layout->blocks), TRYTR_DEFAULT_NODE_LIMIT, &state->status);
    if (items == NULL) return NULL;
    layout->blocks = items;
    TrytrLayoutBlock *block = &layout->blocks[layout->block_count++];
    *block = (TrytrLayoutBlock){.kind = kind,
        .first_element = layout->element_count - 1u, .element_count = 1u,
        .first_line = layout->line_count};
    return block;
}

static bool layout_text(FlowState *state, const TrytrDocumentElement *element)
{
    const TrytrLayoutBlockKind kind = element->kind == TRYTR_ELEMENT_HEADING
        ? TRYTR_LAYOUT_HEADING : element->kind == TRYTR_ELEMENT_MATH
            ? TRYTR_LAYOUT_MATH : TRYTR_LAYOUT_TEXT;
    TrytrLayoutBlock *block = add_block(state, kind);
    if (block == NULL) return false;
    trytr_layout_text_metrics(element, &block->font_size, &block->line_height);
    TrytrTextLineIterator iterator = trytr_layout_text_iterator(element, block->font_size);
    if (kind != TRYTR_LAYOUT_MATH && state->options.width > 0 &&
        (iterator.wrap_width == 0 || iterator.wrap_width > state->options.width)) {
        iterator.wrap_width = state->options.width;
    }
    TrytrTextLine line;
    while (trytr_layout_text_line_next(&iterator, &line)) {
        TrytrLayout *layout = state->layout;
        void *items = grow(layout->lines, layout->line_count, &layout->line_capacity,
                           sizeof(*layout->lines), LAYOUT_LINE_LIMIT, &state->status);
        if (items == NULL) return false;
        layout->lines = items;
        layout->lines[layout->line_count++] = (TrytrLayoutLine){
            .start = line.start, .length = line.end - line.start, .width = line.width};
        if (line.width > block->width) block->width = line.width;
        ++block->line_count;
    }
    if (iterator.wrap_width > block->width) block->width = iterator.wrap_width;
    block->height = (int64_t)block->line_count * block->line_height;
    return true;
}

static bool collect(const TrytrDocumentElement *element, void *user_data)
{
    FlowState *state = user_data;
    if (element->kind == TRYTR_ELEMENT_GROUP_BEGIN) {
        if (state->depth >= TRYTR_DEFAULT_DEPTH_LIMIT) return false;
        state->stack_x[state->depth] = state->tx;
        state->stack_y[state->depth++] = state->ty;
        state->tx += element->x;
        state->ty += element->y;
        return true;
    }
    if (element->kind == TRYTR_ELEMENT_GROUP_END) {
        if (state->depth == 0u) return false;
        state->tx = state->stack_x[--state->depth];
        state->ty = state->stack_y[state->depth];
        return true;
    }
    if (!add_element(state, element)) return false;
    if (element->kind == TRYTR_ELEMENT_HEADING || element->kind == TRYTR_ELEMENT_TEXT ||
        element->kind == TRYTR_ELEMENT_MATH) return layout_text(state, element);

    TrytrLayout *layout = state->layout;
    TrytrLayoutBlock *block = layout->block_count == 0u ? NULL
        : &layout->blocks[layout->block_count - 1u];
    const bool existing_canvas = block != NULL && block->kind == TRYTR_LAYOUT_CANVAS;
    if (!existing_canvas) block = add_block(state, TRYTR_LAYOUT_CANVAS);
    else ++block->element_count;
    if (block == NULL) return false;

    TrytrLayoutBounds bounds;
    if (!trytr_layout_geometry_bounds(element, state->tx, state->ty, &bounds)) return false;
    if (state->fixed_canvas) {
        block->width = state->canvas_width;
        block->height = state->canvas_height;
    } else if (!existing_canvas) {
        block->canvas_x = bounds.min_x - 16;
        block->canvas_y = bounds.min_y - 16;
        block->width = bounds.max_x - bounds.min_x + 32;
        block->height = bounds.max_y - bounds.min_y + 32;
    } else {
        const int64_t right = block->canvas_x + block->width;
        const int64_t bottom = block->canvas_y + block->height;
        if (bounds.min_x - 16 < block->canvas_x) block->canvas_x = bounds.min_x - 16;
        if (bounds.min_y - 16 < block->canvas_y) block->canvas_y = bounds.min_y - 16;
        block->width = (bounds.max_x + 16 > right ? bounds.max_x + 16 : right) - block->canvas_x;
        block->height = (bounds.max_y + 16 > bottom ? bounds.max_y + 16 : bottom) - block->canvas_y;
    }
    return true;
}

TrytrStatus trytr_layout_create(TrytrDocument *document,
                                const TrytrLayoutOptions *options,
                                TrytrLayout **out_layout)
{
    if (out_layout != NULL) *out_layout = NULL;
    if (document == NULL || out_layout == NULL ||
        (options != NULL && (options->width < 0 || options->gap < 0 ||
         options->width > TRYTR_DEFAULT_COORDINATE_LIMIT ||
         options->gap > TRYTR_DEFAULT_COORDINATE_LIMIT))) return TRYTR_STATUS_INVALID_ARGUMENT;
    TrytrLayout *layout = calloc(1u, sizeof(*layout));
    if (layout == NULL) return TRYTR_STATUS_NO_MEMORY;
    FlowState state = {.layout = layout, .status = TRYTR_STATUS_OK,
        .options = options == NULL ? (TrytrLayoutOptions){.gap = 8} : *options};
    state.fixed_canvas = trytr_document_get_canvas_size(
        document, &state.canvas_width, &state.canvas_height);
    TrytrStatus status = trytr_document_execute(document, collect, &state);
    if (state.status != TRYTR_STATUS_OK) status = state.status;
    int64_t y = 16;
    for (size_t index = 0u; status == TRYTR_STATUS_OK && index < layout->block_count; ++index) {
        TrytrLayoutBlock *block = &layout->blocks[index];
        block->x = 16;
        block->y = y;
        if (block->kind != TRYTR_LAYOUT_CANVAS) {
            const TrytrLayoutElement *element = &layout->elements[block->first_element];
            block->x += element->translate_x;
            block->y += element->translate_y;
        }
        const int64_t bottom = block->y + block->height;
        if (block->x < -LAYOUT_EXTENT_LIMIT || block->y < -LAYOUT_EXTENT_LIMIT ||
            block->x + block->width > LAYOUT_EXTENT_LIMIT || bottom > LAYOUT_EXTENT_LIMIT) {
            status = TRYTR_STATUS_LIMIT_EXCEEDED;
        }
        if (bottom > y) y = bottom;
        y += state.options.gap;
    }
    if (status != TRYTR_STATUS_OK) { trytr_layout_destroy(layout); return status; }
    *out_layout = layout;
    return TRYTR_STATUS_OK;
}

void trytr_layout_destroy(TrytrLayout *layout)
{
    if (layout == NULL) return;
    free(layout->blocks);
    free(layout->elements);
    free(layout->lines);
    free(layout);
}

size_t trytr_layout_block_count(const TrytrLayout *layout)
{ return layout == NULL ? 0u : layout->block_count; }

const TrytrLayoutBlock *trytr_layout_block_at(const TrytrLayout *layout, size_t index)
{ return layout == NULL || index >= layout->block_count ? NULL : &layout->blocks[index]; }

const TrytrLayoutElement *trytr_layout_element_at(const TrytrLayout *layout, size_t index)
{ return layout == NULL || index >= layout->element_count ? NULL : &layout->elements[index]; }

const TrytrLayoutLine *trytr_layout_line_at(const TrytrLayout *layout, size_t index)
{ return layout == NULL || index >= layout->line_count ? NULL : &layout->lines[index]; }
