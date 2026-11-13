#include "text_render.h"
#include "text_render_internal.h"

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

static void include_bounds(TextState *state, int64_t x1, int64_t y1,
                           int64_t x2, int64_t y2)
{
    const int64_t left = x1 < x2 ? x1 : x2;
    const int64_t top = y1 < y2 ? y1 : y2;
    const int64_t right = x1 > x2 ? x1 : x2;
    const int64_t bottom = y1 > y2 ? y1 : y2;
    if (!state->have_bounds) {
        state->min_x = left;
        state->min_y = top;
        state->max_x = right;
        state->max_y = bottom;
        state->have_bounds = true;
        return;
    }
    if (left < state->min_x) state->min_x = left;
    if (top < state->min_y) state->min_y = top;
    if (right > state->max_x) state->max_x = right;
    if (bottom > state->max_y) state->max_y = bottom;
}

static bool collect_element(const TrytrDocumentElement *element, void *user_data)
{
    TextState *state = user_data;
    if (element->kind == TRYTR_ELEMENT_GROUP_BEGIN) {
        if (state->group_depth >= TRYTR_DEFAULT_DEPTH_LIMIT) return false;
        state->stack_x[state->group_depth] = state->group_x;
        state->stack_y[state->group_depth] = state->group_y;
        ++state->group_depth;
        state->group_x += element->x;
        state->group_y += element->y;
        return true;
    }
    if (element->kind == TRYTR_ELEMENT_GROUP_END) {
        if (state->group_depth == 0u) return false;
        --state->group_depth;
        state->group_x = state->stack_x[state->group_depth];
        state->group_y = state->stack_y[state->group_depth];
        return true;
    }
    if (element->kind == TRYTR_ELEMENT_HEADING || element->kind == TRYTR_ELEMENT_TEXT ||
        element->kind == TRYTR_ELEMENT_MATH) {
        if (state->output.length != 0u && state->output.data[state->output.length - 1u] != '\n' &&
            !trytr_text_buffer_append(&state->output, "\n", 1u)) return false;
        if (element->kind == TRYTR_ELEMENT_HEADING &&
            !trytr_text_buffer_append(&state->output, "# ", 2u)) return false;
        if (element->kind == TRYTR_ELEMENT_MATH &&
            !trytr_text_buffer_append(&state->output, "= ", 2u)) return false;
        return append_source_text(&state->output, element->text, element->text_length) &&
               trytr_text_buffer_append(&state->output, "\n", 1u);
    }

    state->has_geometry = true;
    switch (element->kind) {
        case TRYTR_ELEMENT_RECT:
        case TRYTR_ELEMENT_ROUNDRECT:
            include_bounds(state,
                           element->x + state->group_x,
                           element->y + state->group_y,
                           element->x + element->width + state->group_x,
                           element->y + element->height + state->group_y);
            break;
        case TRYTR_ELEMENT_LINE:
            include_bounds(state, element->x + state->group_x,
                           element->y + state->group_y,
                           element->x2 + state->group_x,
                           element->y2 + state->group_y);
            break;
        case TRYTR_ELEMENT_CIRCLE:
            include_bounds(state,
                           element->x - element->radius + state->group_x,
                           element->y - element->radius + state->group_y,
                           element->x + element->radius + state->group_x,
                           element->y + element->radius + state->group_y);
            break;
        case TRYTR_ELEMENT_ELLIPSE:
            include_bounds(state,
                           element->x - element->radius_x + state->group_x,
                           element->y - element->radius_y + state->group_y,
                           element->x + element->radius_x + state->group_x,
                           element->y + element->radius_y + state->group_y);
            break;
        case TRYTR_ELEMENT_PLOT:
            include_bounds(state,
                           element->x + state->group_x,
                           element->y + state->group_y,
                           element->x + element->width + state->group_x,
                           element->y + element->height + state->group_y);
            break;
        case TRYTR_ELEMENT_PATH:
        case TRYTR_ELEMENT_POLYGON:
            include_bounds(state,
                           element->min_x + state->group_x,
                           element->min_y + state->group_y,
                           element->max_x + state->group_x,
                           element->max_y + state->group_y);
            break;
        default:
            break;
    }
    return true;
}


TrytrStatus trytr_text_render(
    TrytrDocument *document,
    char **out_text,
    size_t *out_length
)
{
    if (document == NULL || out_text == NULL || out_length == NULL)
        return TRYTR_STATUS_INVALID_ARGUMENT;
    *out_text = NULL;
    *out_length = 0u;
    TextState state = {0};
    state.output.status = TRYTR_STATUS_OK;
    TrytrStatus status = trytr_document_execute(document, collect_element, &state);
    if (status != TRYTR_STATUS_OK) {
        if (state.output.status != TRYTR_STATUS_OK) status = state.output.status;
        goto fail;
    }
    if (state.output.status != TRYTR_STATUS_OK) {
        status = state.output.status;
        goto fail;
    }
    if (!state.has_geometry) {
        if (state.output.data == NULL) {
            state.output.data = calloc(1u, 1u);
            if (state.output.data == NULL) { status = TRYTR_STATUS_NO_MEMORY; goto fail; }
        }
        *out_text = state.output.data;
        *out_length = state.output.length;
        return TRYTR_STATUS_OK;
    }

    int64_t canvas_width = 0;
    int64_t canvas_height = 0;
    const bool fixed_canvas = trytr_document_get_canvas_size(
        document, &canvas_width, &canvas_height);
    if (fixed_canvas) {
        state.min_x = 0;
        state.min_y = 0;
        state.max_x = canvas_width;
        state.max_y = canvas_height;
    } else {
        state.min_x -= 1;
        state.min_y -= 1;
        state.max_x += 1;
        state.max_y += 1;
    }
    trytr_text_grid_init(&state, state.max_x - state.min_x, state.max_y - state.min_y);
    status = trytr_document_execute(document, trytr_text_draw_element, &state);
    if (status != TRYTR_STATUS_OK) goto fail;
    if (state.output.length != 0u && state.output.data[state.output.length - 1u] == '\n' &&
        !trytr_text_buffer_append(&state.output, "\n", 1u)) {
        status = state.output.status;
        goto fail;
    }
    if (!trytr_text_append_grid(&state)) {
        status = state.output.status;
        goto fail;
    }
    *out_text = state.output.data;
    *out_length = state.output.length;
    return TRYTR_STATUS_OK;

fail:
    free(state.output.data);
    return status;
}
