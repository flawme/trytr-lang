#include "text_render.h"
#include "text_render_internal.h"
#include "trytr/layout.h"

#include <stdlib.h>
#include <string.h>


static bool text_reserve(TextBuffer *buffer, size_t extra)
{
    if (buffer->status != TRYTR_STATUS_OK) return false;
    if (extra > TEXT_OUTPUT_LIMIT - buffer->length) {
        buffer->status = TRYTR_STATUS_LIMIT_EXCEEDED;
        return false;
    }
    const size_t required = buffer->length + extra + 1u;
    if (required <= buffer->capacity) return true;
    size_t capacity = buffer->capacity == 0u ? 1024u : buffer->capacity;
    while (capacity < required) {
        if (capacity > TEXT_OUTPUT_LIMIT / 2u) {
            capacity = TEXT_OUTPUT_LIMIT + 1u;
            break;
        }
        capacity *= 2u;
    }
    char *replacement = realloc(buffer->data, capacity);
    if (replacement == NULL) {
        buffer->status = TRYTR_STATUS_NO_MEMORY;
        return false;
    }
    buffer->data = replacement;
    buffer->capacity = capacity;
    return true;
}

bool trytr_text_buffer_append(TextBuffer *buffer, const char *data, size_t length)
{
    if (!text_reserve(buffer, length)) return false;
    if (length != 0u) memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

static bool append_source_text(TextBuffer *buffer, const char *text, size_t length)
{
    for (size_t index = 0u; index < length; ++index) {
        const unsigned char byte = (unsigned char)text[index];
        if (byte == '\n' || byte == '\t' || (byte >= 0x20u && byte != 0x7fu)) {
            if (!trytr_text_buffer_append(buffer, text + index, 1u)) return false;
        } else if (!trytr_text_buffer_append(buffer, " ", 1u)) {
            return false;
        }
    }
    return true;
}

static bool append_text_block(TextState *state, const TrytrLayout *layout,
                               const TrytrLayoutBlock *block)
{
    const TrytrLayoutElement *item = trytr_layout_element_at(layout, block->first_element);
    if (block->kind == TRYTR_LAYOUT_HEADING &&
        !trytr_text_buffer_append(&state->output, "# ", 2u)) return false;
    if (block->kind == TRYTR_LAYOUT_MATH &&
        !trytr_text_buffer_append(&state->output, "= ", 2u)) return false;
    for (size_t index = 0u; index < block->line_count; ++index) {
        const TrytrLayoutLine *line = trytr_layout_line_at(layout, block->first_line + index);
        if (!append_source_text(&state->output, item->element.text + line->start, line->length) ||
            !trytr_text_buffer_append(&state->output, "\n", 1u)) return false;
    }
    return true;
}

static bool append_canvas(TextState *state, const TrytrLayout *layout,
                           const TrytrLayoutBlock *block)
{
    state->min_x = block->canvas_x;
    state->min_y = block->canvas_y;
    state->max_x = block->canvas_x + block->width;
    state->max_y = block->canvas_y + block->height;
    trytr_text_grid_init(state, block->width, block->height);
    for (size_t index = 0u; index < block->element_count; ++index) {
        const TrytrLayoutElement *item = trytr_layout_element_at(layout, block->first_element + index);
        state->group_x = item->translate_x;
        state->group_y = item->translate_y;
        if (!trytr_text_draw_element(&item->element, state)) return false;
    }
    return trytr_text_append_grid(state);
}

TrytrStatus trytr_text_render(TrytrDocument *document, char **out_text, size_t *out_length)
{
    if (out_text != NULL) *out_text = NULL;
    if (out_length != NULL) *out_length = 0u;
    if (document == NULL || out_text == NULL || out_length == NULL)
        return TRYTR_STATUS_INVALID_ARGUMENT;
    TrytrLayout *layout = NULL;
    TrytrStatus status = trytr_layout_create(document, NULL, &layout);
    if (status != TRYTR_STATUS_OK) return status;
    TextState state = {0};
    bool after_canvas = false;
    for (size_t index = 0u; index < trytr_layout_block_count(layout); ++index) {
        const TrytrLayoutBlock *block = trytr_layout_block_at(layout, index);
        if ((after_canvas || (block->kind == TRYTR_LAYOUT_CANVAS && state.output.length != 0u)) &&
            !trytr_text_buffer_append(&state.output, "\n", 1u)) break;
        const bool rendered = block->kind == TRYTR_LAYOUT_CANVAS
            ? append_canvas(&state, layout, block) : append_text_block(&state, layout, block);
        if (!rendered) {
            status = state.output.status == TRYTR_STATUS_OK
                ? TRYTR_STATUS_EXECUTION_FAILED : state.output.status;
            break;
        }
        after_canvas = block->kind == TRYTR_LAYOUT_CANVAS;
    }
    if (state.output.status != TRYTR_STATUS_OK) status = state.output.status;
    trytr_layout_destroy(layout);
    if (status == TRYTR_STATUS_OK && state.output.data == NULL) {
        state.output.data = calloc(1u, 1u);
        if (state.output.data == NULL) status = TRYTR_STATUS_NO_MEMORY;
    }
    if (status != TRYTR_STATUS_OK) { free(state.output.data); return status; }
    *out_text = state.output.data;
    *out_length = state.output.length;
    return TRYTR_STATUS_OK;
}
