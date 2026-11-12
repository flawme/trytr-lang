#include "svg_render.h"

#include "limits.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct SvgBuffer {
    char *data;
    size_t length;
    size_t capacity;
    TrytrStatus status;
} SvgBuffer;

typedef struct SvgState {
    SvgBuffer output;
    bool have_bounds;
    int64_t min_x;
    int64_t min_y;
    int64_t max_x;
    int64_t max_y;
    int64_t flow_y;
    int64_t group_translate_x;
    int64_t group_translate_y;
    int64_t group_stack_x[TRYTR_DEFAULT_DEPTH_LIMIT];
    int64_t group_stack_y[TRYTR_DEFAULT_DEPTH_LIMIT];
    size_t group_depth;
} SvgState;

static bool buffer_reserve(SvgBuffer *buffer, size_t extra)
{
    if (buffer->status != TRYTR_STATUS_OK) return false;
    if (extra > TRYTR_DEFAULT_SVG_OUTPUT_LIMIT - buffer->length) {
        buffer->status = TRYTR_STATUS_LIMIT_EXCEEDED;
        return false;
    }
    const size_t required = buffer->length + extra + 1u;
    if (required <= buffer->capacity) return true;

    size_t capacity = buffer->capacity == 0u ? 1024u : buffer->capacity;
    while (capacity < required) {
        const size_t maximum = (size_t)TRYTR_DEFAULT_SVG_OUTPUT_LIMIT + 1u;
        if (capacity > maximum / 2u) {
            capacity = maximum;
            break;
        }
        capacity *= 2u;
    }
    if (capacity < required) {
        buffer->status = TRYTR_STATUS_LIMIT_EXCEEDED;
        return false;
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

static bool buffer_append(SvgBuffer *buffer, const char *text, size_t length)
{
    if (!buffer_reserve(buffer, length)) return false;
    if (length != 0u) memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

static bool buffer_format(SvgBuffer *buffer, const char *format, ...)
{
    if (buffer->status != TRYTR_STATUS_OK) return false;
    va_list arguments;
    va_start(arguments, format);
    va_list sizing_arguments;
    va_copy(sizing_arguments, arguments);
    const int needed = vsnprintf(NULL, 0u, format, sizing_arguments);
    va_end(sizing_arguments);
    if (needed < 0) {
        va_end(arguments);
        buffer->status = TRYTR_STATUS_EXECUTION_FAILED;
        return false;
    }
    const size_t length = (size_t)needed;
    if (!buffer_reserve(buffer, length)) {
        va_end(arguments);
        return false;
    }
    (void)vsnprintf(buffer->data + buffer->length,
                    buffer->capacity - buffer->length, format, arguments);
    va_end(arguments);
    buffer->length += length;
    return true;
}

static void include_bounds(SvgState *state, int64_t left, int64_t top,
                           int64_t right, int64_t bottom)
{
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

static size_t line_end(const char *text, size_t length, size_t start)
{
    size_t end = start;
    while (end < length && text[end] != '\n' && text[end] != '\r') ++end;
    return end;
}

static size_t next_line_start(const char *text, size_t length, size_t end)
{
    if (end >= length) return length;
    if (text[end] == '\r' && end + 1u < length && text[end + 1u] == '\n') {
        return end + 2u;
    }
    return end + 1u;
}

static bool layout_text(SvgState *state, const TrytrDocumentElement *element)
{
    int64_t font_size;
    int64_t line_height;
    switch (element->kind) {
        case TRYTR_ELEMENT_HEADING: font_size = 28; line_height = 36; break;
        case TRYTR_ELEMENT_MATH: font_size = 18; line_height = 26; break;
        case TRYTR_ELEMENT_TEXT: font_size = 16; line_height = 22; break;
        case TRYTR_ELEMENT_RECT:
        case TRYTR_ELEMENT_LINE:
        case TRYTR_ELEMENT_CIRCLE: return true;
        case TRYTR_ELEMENT_ELLIPSE: return true;
        case TRYTR_ELEMENT_PATH: return true;
        default: return false;
    }
    if (element->text == NULL && element->text_length != 0u) return false;
    const char *text = element->text == NULL ? "" : element->text;

    size_t start = 0u;
    int64_t top = state->flow_y;
    for (;;) {
        const size_t end = line_end(text, element->text_length, start);
        const size_t byte_count = end - start;
        const int64_t estimated_width = (int64_t)byte_count * font_size;
        include_bounds(state,
                       16 + state->group_translate_x,
                       top + state->group_translate_y,
                       16 + estimated_width + state->group_translate_x,
                       top + line_height + state->group_translate_y);
        top += line_height;
        if (end == element->text_length) break;
        start = next_line_start(text, element->text_length, end);
    }
    state->flow_y = top + 8;
    return true;
}

static bool measure_element(const TrytrDocumentElement *element, void *user_data)
{
    SvgState *state = user_data;
    if (element->kind == TRYTR_ELEMENT_GROUP_BEGIN) {
        if (state->group_depth >= sizeof(state->group_stack_x) / sizeof(state->group_stack_x[0])) {
            return false;
        }
        state->group_stack_x[state->group_depth] = state->group_translate_x;
        state->group_stack_y[state->group_depth] = state->group_translate_y;
        ++state->group_depth;
        state->group_translate_x += element->x;
        state->group_translate_y += element->y;
        return true;
    }
    if (element->kind == TRYTR_ELEMENT_GROUP_END) {
        if (state->group_depth == 0u) return false;
        --state->group_depth;
        state->group_translate_x = state->group_stack_x[state->group_depth];
        state->group_translate_y = state->group_stack_y[state->group_depth];
        return true;
    }
    switch (element->kind) {
        case TRYTR_ELEMENT_RECT:
            include_bounds(state,
                           element->x + state->group_translate_x,
                           element->y + state->group_translate_y,
                           element->x + element->width + state->group_translate_x,
                           element->y + element->height + state->group_translate_y);
            return true;
        case TRYTR_ELEMENT_LINE: {
            const int64_t left = element->x < element->x2 ? element->x : element->x2;
            const int64_t top = element->y < element->y2 ? element->y : element->y2;
            const int64_t right = element->x > element->x2 ? element->x : element->x2;
            const int64_t bottom = element->y > element->y2 ? element->y : element->y2;
            include_bounds(state,
                           left + state->group_translate_x,
                           top + state->group_translate_y,
                           right + state->group_translate_x,
                           bottom + state->group_translate_y);
            return true;
        }
        case TRYTR_ELEMENT_CIRCLE:
            include_bounds(state,
                           element->x - element->radius + state->group_translate_x,
                           element->y - element->radius + state->group_translate_y,
                           element->x + element->radius + state->group_translate_x,
                           element->y + element->radius + state->group_translate_y);
            return true;
        case TRYTR_ELEMENT_ELLIPSE:
            include_bounds(state,
                           element->x - element->radius_x + state->group_translate_x,
                           element->y - element->radius_y + state->group_translate_y,
                           element->x + element->radius_x + state->group_translate_x,
                           element->y + element->radius_y + state->group_translate_y);
            return true;
        case TRYTR_ELEMENT_PATH:
            include_bounds(state,
                           element->min_x + state->group_translate_x,
                           element->min_y + state->group_translate_y,
                           element->max_x + state->group_translate_x,
                           element->max_y + state->group_translate_y);
            return true;
        default:
            return layout_text(state, element);
    }
}

static bool append_xml_text(SvgBuffer *buffer, const char *text, size_t length)
{
    static const char replacement[] = "\xEF\xBF\xBD";
    size_t offset = 0u;
    while (offset < length) {
        const unsigned char first = (unsigned char)text[offset];
        size_t width = first <= 0x7Fu ? 1u :
                       first <= 0xDFu ? 2u : first <= 0xEFu ? 3u : 4u;
        uint32_t codepoint = width == 1u ? (uint32_t)first :
            (uint32_t)(first & (width == 2u ? 0x1Fu : width == 3u ? 0x0Fu : 0x07u));
        for (size_t index = 1u; index < width; ++index) {
            codepoint = (codepoint << 6u) |
                        ((uint32_t)(unsigned char)text[offset + index] & 0x3Fu);
        }
        const bool xml_valid = !(codepoint < 0x20u && codepoint != 0x09u) &&
                               (codepoint & 0xFFFFu) != 0xFFFEu &&
                               (codepoint & 0xFFFFu) != 0xFFFFu;
        if (!xml_valid) {
            if (!buffer_append(buffer, replacement, sizeof(replacement) - 1u)) return false;
        } else if (width == 1u && first == (unsigned char)'&') {
            if (!buffer_append(buffer, "&amp;", 5u)) return false;
        } else if (width == 1u && first == (unsigned char)'<') {
            if (!buffer_append(buffer, "&lt;", 4u)) return false;
        } else if (width == 1u && first == (unsigned char)'>') {
            if (!buffer_append(buffer, "&gt;", 4u)) return false;
        } else if (!buffer_append(buffer, text + offset, width)) {
            return false;
        }
        offset += width;
    }
    return true;
}

static bool render_text(SvgState *state, const TrytrDocumentElement *element)
{
    const char *family = "sans-serif";
    int64_t font_size = 16;
    int64_t line_height = 22;
    const char *weight = "400";
    const char *style = "normal";
    if (element->kind == TRYTR_ELEMENT_HEADING) {
        font_size = 28;
        line_height = 36;
        weight = "700";
    } else if (element->kind == TRYTR_ELEMENT_MATH) {
        family = "serif";
        font_size = 18;
        line_height = 26;
        style = "italic";
    }
    if (!buffer_format(&state->output,
                       "<text x=\"16\" y=\"%" PRId64
                       "\" font-family=\"%s\" font-size=\"%" PRId64
                       "\" font-weight=\"%s\" font-style=\"%s\"",
                       state->flow_y + font_size, family, font_size, weight, style)) return false;
    if (element->id != NULL &&
        !buffer_format(&state->output, " id=\"%s\"", element->id)) return false;
    if (!buffer_append(&state->output, ">", 1u)) return false;

    const char *text = element->text == NULL ? "" : element->text;
    size_t start = 0u;
    bool first_line = true;
    for (;;) {
        const size_t end = line_end(text, element->text_length, start);
        if (!buffer_format(&state->output,
                           "<tspan x=\"16\" dy=\"%" PRId64 "\">",
                           first_line ? 0 : line_height) ||
            !append_xml_text(&state->output, text + start, end - start) ||
            !buffer_append(&state->output, "</tspan>", 8u)) return false;
        first_line = false;
        if (end == element->text_length) break;
        start = next_line_start(text, element->text_length, end);
    }
    if (!buffer_append(&state->output, "</text>\n", 8u)) return false;

    size_t line_start = 0u;
    int64_t top = state->flow_y;
    for (;;) {
        const size_t end = line_end(text, element->text_length, line_start);
        top += line_height;
        if (end == element->text_length) break;
        line_start = next_line_start(text, element->text_length, end);
    }
    state->flow_y = top + 8;
    return true;
}

static bool render_element(const TrytrDocumentElement *element, void *user_data)
{
    SvgState *state = user_data;
    if (element->kind == TRYTR_ELEMENT_GROUP_BEGIN) {
        if (!buffer_append(&state->output, "<g", 2u)) return false;
        if (element->id != NULL &&
            !buffer_format(&state->output, " id=\"%s\"", element->id)) return false;
        return buffer_format(&state->output,
                             " transform=\"translate(%" PRId64 " %" PRId64 ")\">\n",
                             element->x, element->y);
    }
    if (element->kind == TRYTR_ELEMENT_GROUP_END) {
        return buffer_append(&state->output, "</g>\n", 5u);
    }
    if (element->kind == TRYTR_ELEMENT_RECT) {
        if (element->id != NULL &&
            !buffer_format(&state->output, "<rect id=\"%s\"", element->id)) return false;
        if (element->id == NULL && !buffer_append(&state->output, "<rect", 5u)) return false;
        return buffer_format(&state->output,
                             " x=\"%" PRId64 "\" y=\"%" PRId64
                             "\" width=\"%" PRId64 "\" height=\"%" PRId64
                             "\" fill=\"none\" stroke=\"#222\" stroke-width=\"1\"/>\n",
                             element->x, element->y, element->width, element->height);
    }
    if (element->kind == TRYTR_ELEMENT_LINE) {
        if (element->id != NULL &&
            !buffer_format(&state->output, "<line id=\"%s\"", element->id)) return false;
        if (element->id == NULL && !buffer_append(&state->output, "<line", 5u)) return false;
        return buffer_format(&state->output,
                             " x1=\"%" PRId64 "\" y1=\"%" PRId64
                             "\" x2=\"%" PRId64 "\" y2=\"%" PRId64
                             "\" stroke=\"#222\" stroke-width=\"1\"/>\n",
                             element->x, element->y, element->x2, element->y2);
    }
    if (element->kind == TRYTR_ELEMENT_CIRCLE) {
        if (element->id != NULL &&
            !buffer_format(&state->output, "<circle id=\"%s\"", element->id)) return false;
        if (element->id == NULL && !buffer_append(&state->output, "<circle", 7u)) return false;
        return buffer_format(&state->output,
                             " cx=\"%" PRId64 "\" cy=\"%" PRId64
                             "\" r=\"%" PRId64
                             "\" fill=\"none\" stroke=\"#222\" stroke-width=\"1\"/>\n",
                             element->x, element->y, element->radius);
    }
    if (element->kind == TRYTR_ELEMENT_ELLIPSE) {
        if (element->id != NULL &&
            !buffer_format(&state->output, "<ellipse id=\"%s\"", element->id)) return false;
        if (element->id == NULL && !buffer_append(&state->output, "<ellipse", 8u)) return false;
        return buffer_format(&state->output,
                             " cx=\"%" PRId64 "\" cy=\"%" PRId64
                             "\" rx=\"%" PRId64 "\" ry=\"%" PRId64
                             "\" fill=\"none\" stroke=\"#222\" stroke-width=\"1\"/>\n",
                             element->x, element->y, element->radius_x, element->radius_y);
    }
    if (element->kind == TRYTR_ELEMENT_PATH) {
        static const char suffix[] =
            "\" fill=\"none\" stroke=\"#222\" stroke-width=\"1\"/>\n";
        if (element->id != NULL &&
            !buffer_format(&state->output, "<path id=\"%s\" d=\"", element->id)) return false;
        if (element->id == NULL && !buffer_append(&state->output, "<path d=\"", 9u)) return false;
        for (size_t index = 0u; index < element->text_length; ++index) {
            const char value = element->text[index];
            if (value == '\n' || value == '\r' || value == '\t') {
                if (!buffer_append(&state->output, " ", 1u)) return false;
            } else if (!buffer_append(&state->output, &value, 1u)) {
                return false;
            }
        }
        return buffer_append(&state->output, suffix, sizeof(suffix) - 1u);
    }
    return render_text(state, element);
}

static TrytrStatus append_svg_header(SvgState *state)
{
    int64_t min_x = state->have_bounds ? state->min_x - 16 : 0;
    int64_t min_y = state->have_bounds ? state->min_y - 16 : 0;
    int64_t width = state->have_bounds ? state->max_x - state->min_x + 32 : 1;
    int64_t height = state->have_bounds ? state->max_y - state->min_y + 32 : 1;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (!buffer_format(&state->output,
                       "<svg xmlns=\"http://www.w3.org/2000/svg\" role=\"img\" "
                       "width=\"%" PRId64 "\" height=\"%" PRId64
                       "\" viewBox=\"%" PRId64 " %" PRId64 " %" PRId64 " %" PRId64
                       "\"><title>trytr document</title>\n",
                       width, height, min_x, min_y, width, height)) {
        return state->output.status;
    }
    return TRYTR_STATUS_OK;
}

TrytrStatus trytr_svg_render(
    TrytrDocument *document,
    char **out_svg,
    size_t *out_length
)
{
    if (document == NULL || out_svg == NULL || out_length == NULL) {
        return TRYTR_STATUS_INVALID_ARGUMENT;
    }
    *out_svg = NULL;
    *out_length = 0u;

    SvgState state = {.flow_y = 16};
    TrytrStatus status = trytr_document_execute(document, measure_element, &state);
    if (status != TRYTR_STATUS_OK) return status;
    status = append_svg_header(&state);
    if (status != TRYTR_STATUS_OK) goto fail;

    state.flow_y = 16;
    status = trytr_document_execute(document, render_element, &state);
    if (status != TRYTR_STATUS_OK) {
        if (state.output.status != TRYTR_STATUS_OK) status = state.output.status;
        goto fail;
    }
    if (!buffer_append(&state.output, "</svg>\n", 7u)) {
        status = state.output.status;
        goto fail;
    }

    *out_svg = state.output.data;
    *out_length = state.output.length;
    return TRYTR_STATUS_OK;

fail:
    free(state.output.data);
    return status;
}
