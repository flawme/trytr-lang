#include "svg_render.h"

#include "limits.h"
#include "trytr/math/curve.h"
#include "plot.h"

#include <inttypes.h>
#include <math.h>
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
    bool has_fixed_canvas;
    int64_t canvas_width;
    int64_t canvas_height;
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

static size_t utf8_scalar_width(unsigned char first)
{
    if (first <= 0x7Fu) return 1u;
    if (first <= 0xDFu) return 2u;
    if (first <= 0xEFu) return 3u;
    return 4u;
}

static size_t utf8_scalar_count(const char *text, size_t length)
{
    size_t count = 0u;
    for (size_t offset = 0u; offset < length; ++count) {
        size_t width = utf8_scalar_width((unsigned char)text[offset]);
        if (width > length - offset) width = 1u;
        offset += width;
    }
    return count;
}

static void text_metrics(const TrytrDocumentElement *element,
                        int64_t *font_size, int64_t *line_height)
{
    int64_t line_gap;
    switch (element->kind) {
        case TRYTR_ELEMENT_HEADING:
            *font_size = 28;
            line_gap = 8;
            break;
        case TRYTR_ELEMENT_MATH:
            *font_size = 18;
            line_gap = 8;
            break;
        default:
            *font_size = 16;
            line_gap = 6;
            break;
    }
    if (element->has_font_size) *font_size = element->font_size;
    *line_height = *font_size + line_gap;
}

static int64_t estimated_text_width(const char *text, size_t length,
                                    int64_t font_size)
{
    return (int64_t)utf8_scalar_count(text, length) * font_size;
}

typedef struct TextLineIterator {
    const char *text;
    size_t length;
    size_t cursor;
    int64_t font_size;
    int64_t wrap_width;
    bool done;
} TextLineIterator;

typedef struct TextLine {
    size_t start;
    size_t end;
    int64_t width;
} TextLine;

static bool is_text_space(char value)
{
    return value == ' ' || value == '\t';
}

static void text_iterator_advance_paragraph(TextLineIterator *iterator,
                                            size_t paragraph_end)
{
    if (paragraph_end == iterator->length) {
        iterator->done = true;
    } else {
        iterator->cursor = next_line_start(iterator->text,
                                           iterator->length,
                                           paragraph_end);
    }
}

static void text_iterator_advance_word(TextLineIterator *iterator,
                                       size_t word_end,
                                       size_t paragraph_end)
{
    size_t next_word = word_end;
    while (next_word < paragraph_end &&
           is_text_space(iterator->text[next_word])) ++next_word;
    if (next_word == paragraph_end) {
        text_iterator_advance_paragraph(iterator, paragraph_end);
    } else {
        iterator->cursor = next_word;
    }
}

static bool text_line_next(TextLineIterator *iterator, TextLine *out_line)
{
    if (iterator == NULL || out_line == NULL || iterator->done) return false;
    const size_t paragraph_end = line_end(iterator->text, iterator->length,
                                          iterator->cursor);
    if (iterator->wrap_width <= 0) {
        out_line->start = iterator->cursor;
        out_line->end = paragraph_end;
        out_line->width = estimated_text_width(
            iterator->text + iterator->cursor,
            paragraph_end - iterator->cursor,
            iterator->font_size);
        text_iterator_advance_paragraph(iterator, paragraph_end);
        return true;
    }

    size_t word_start = iterator->cursor;
    while (word_start < paragraph_end &&
           is_text_space(iterator->text[word_start])) ++word_start;
    if (word_start == paragraph_end) {
        out_line->start = word_start;
        out_line->end = word_start;
        out_line->width = 0;
        text_iterator_advance_paragraph(iterator, paragraph_end);
        return true;
    }

    const size_t visual_start = word_start;
    size_t last_word_end = word_start;
    int64_t width = 0;
    for (;;) {
        size_t word_end = word_start;
        while (word_end < paragraph_end &&
               !is_text_space(iterator->text[word_end])) {
            size_t scalar_width = utf8_scalar_width(
                (unsigned char)iterator->text[word_end]);
            if (scalar_width > paragraph_end - word_end) scalar_width = 1u;
            word_end += scalar_width;
        }
        const int64_t word_width = estimated_text_width(
            iterator->text + word_start, word_end - word_start,
            iterator->font_size);
        const int64_t gap_width = width == 0
            ? 0
            : (int64_t)(word_start - last_word_end) * iterator->font_size;
        const int64_t candidate_width = width + gap_width + word_width;

        if (width > 0 && candidate_width > iterator->wrap_width) {
            out_line->start = visual_start;
            out_line->end = last_word_end;
            out_line->width = width;
            iterator->cursor = word_start;
            return true;
        }
        if (width == 0 && word_width > iterator->wrap_width) {
            out_line->start = visual_start;
            out_line->end = word_end;
            out_line->width = word_width;
            text_iterator_advance_word(iterator, word_end, paragraph_end);
            return true;
        }

        width = candidate_width;
        last_word_end = word_end;
        size_t next_word = word_end;
        while (next_word < paragraph_end &&
               is_text_space(iterator->text[next_word])) ++next_word;
        if (next_word == paragraph_end) {
            out_line->start = visual_start;
            out_line->end = last_word_end;
            out_line->width = width;
            text_iterator_advance_paragraph(iterator, paragraph_end);
            return true;
        }
        word_start = next_word;
    }
}

static TextLineIterator text_line_iterator(const TrytrDocumentElement *element,
                                           int64_t font_size)
{
    return (TextLineIterator){
        .text = element->text == NULL ? "" : element->text,
        .length = element->text_length,
        .font_size = font_size,
        .wrap_width = element->has_text_width &&
                      (element->kind == TRYTR_ELEMENT_HEADING ||
                       element->kind == TRYTR_ELEMENT_TEXT)
            ? element->text_width : 0
    };
}

static bool layout_text(SvgState *state, const TrytrDocumentElement *element)
{
    if (element->kind != TRYTR_ELEMENT_HEADING &&
        element->kind != TRYTR_ELEMENT_TEXT &&
        element->kind != TRYTR_ELEMENT_MATH) return false;
    int64_t font_size;
    int64_t line_height;
    text_metrics(element, &font_size, &line_height);
    if (element->text == NULL && element->text_length != 0u) return false;
    TextLineIterator iterator = text_line_iterator(element, font_size);
    TextLine line;
    int64_t maximum_width = 0;
    while (text_line_next(&iterator, &line)) {
        if (line.width > maximum_width) maximum_width = line.width;
    }
    const int64_t alignment_width = element->has_text_width
        ? element->text_width : maximum_width;

    iterator = text_line_iterator(element, font_size);
    int64_t top = state->flow_y;
    while (text_line_next(&iterator, &line)) {
        int64_t left = 16;
        if (element->has_text_alignment &&
            element->text_alignment == TRYTR_TEXT_ALIGN_CENTER) {
            left += alignment_width / 2 - line.width / 2;
        } else if (element->has_text_alignment &&
                   element->text_alignment == TRYTR_TEXT_ALIGN_RIGHT) {
            left += alignment_width - line.width;
        }
        include_bounds(state,
                       left + state->group_translate_x,
                       top + state->group_translate_y,
                       left + line.width + state->group_translate_x,
                       top + line_height + state->group_translate_y);
        top += line_height;
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
        case TRYTR_ELEMENT_ROUNDRECT:
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
        case TRYTR_ELEMENT_POLYGON:
            include_bounds(state,
                           element->min_x + state->group_translate_x,
                           element->min_y + state->group_translate_y,
                           element->max_x + state->group_translate_x,
                           element->max_y + state->group_translate_y);
            return true;
        case TRYTR_ELEMENT_PLOT:
            include_bounds(state,
                           element->x - 48 + state->group_translate_x,
                           element->y + state->group_translate_y,
                           element->x + element->width + 48 + state->group_translate_x,
                           element->y + element->height + 24 + state->group_translate_y);
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
        if (width > length - offset) {
            if (!buffer_append(buffer, replacement, sizeof(replacement) - 1u)) {
                return false;
            }
            ++offset;
            continue;
        }
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

static bool math_group_end(const char *text, size_t length, size_t open,
                           size_t *out_close)
{
    size_t nesting = 1u;
    for (size_t offset = open + 1u; offset < length; ++offset) {
        if (text[offset] == '\\' && offset + 1u < length &&
            (text[offset + 1u] == '_' || text[offset + 1u] == '^' ||
             text[offset + 1u] == '{' || text[offset + 1u] == '}' ||
             text[offset + 1u] == '\\')) {
            ++offset;
        } else if (text[offset] == '{') {
            ++nesting;
        } else if (text[offset] == '}' && --nesting == 0u) {
            *out_close = offset;
            return true;
        }
    }
    return false;
}

static bool append_math_text(SvgBuffer *buffer, const char *text,
                             size_t *cursor, size_t end, size_t depth)
{
    static const char root_prefix[] =
        "<tspan>√</tspan><tspan text-decoration=\"overline\">";
    static const char fraction_slash[] = "⁄";
    while (*cursor < end) {
        const size_t start = *cursor;
        const char value = text[start];
        if (value == '\\' && start + 5u < end &&
            memcmp(text + start + 1u, "sqrt", 4u) == 0 &&
            text[start + 5u] == '{') {
            size_t close = 0u;
            if (depth < TRYTR_DEFAULT_MATH_SCRIPT_DEPTH &&
                math_group_end(text, end, start + 5u, &close)) {
                if (!buffer_append(buffer, root_prefix, sizeof(root_prefix) - 1u)) return false;
                size_t inner = start + 6u;
                if (!append_math_text(buffer, text, &inner, close, depth + 1u) ||
                    !buffer_append(buffer, "</tspan>", 8u)) return false;
                *cursor = close + 1u;
                continue;
            }
        }
        if (value == '\\' && start + 5u < end &&
            memcmp(text + start + 1u, "frac", 4u) == 0 &&
            text[start + 5u] == '{' && depth < TRYTR_DEFAULT_MATH_SCRIPT_DEPTH) {
            size_t numerator_close = 0u;
            if (math_group_end(text, end, start + 5u, &numerator_close) &&
                numerator_close + 1u < end && text[numerator_close + 1u] == '{') {
                size_t denominator_close = 0u;
                if (math_group_end(text, end, numerator_close + 1u,
                                   &denominator_close)) {
                    if (!buffer_append(buffer,
                                       "<tspan baseline-shift=\"super\" font-size=\"70%\">",
                                       sizeof("<tspan baseline-shift=\"super\" font-size=\"70%\">") - 1u)) {
                        return false;
                    }
                    size_t numerator = start + 6u;
                    if (!append_math_text(buffer, text, &numerator,
                                          numerator_close, depth + 1u) ||
                        !buffer_append(buffer, "</tspan>", 8u) ||
                        !append_xml_text(buffer, fraction_slash,
                                         sizeof(fraction_slash) - 1u) ||
                        !buffer_append(buffer,
                                       "<tspan baseline-shift=\"sub\" font-size=\"70%\">",
                                       sizeof("<tspan baseline-shift=\"sub\" font-size=\"70%\">") - 1u)) {
                        return false;
                    }
                    size_t denominator = numerator_close + 2u;
                    if (!append_math_text(buffer, text, &denominator,
                                          denominator_close, depth + 1u) ||
                        !buffer_append(buffer, "</tspan>", 8u)) return false;
                    *cursor = denominator_close + 1u;
                    continue;
                }
            }
        }
        if (value == '\\' && start + 1u < end &&
            (text[start + 1u] == '_' || text[start + 1u] == '^' ||
             text[start + 1u] == '{' || text[start + 1u] == '}' ||
             text[start + 1u] == '\\')) {
            if (!append_xml_text(buffer, text + start + 1u, 1u)) return false;
            *cursor += 2u;
            continue;
        }
        if ((value == '_' || value == '^') && start + 1u < end) {
            size_t argument_start = start + 1u;
            size_t argument_end = argument_start;
            bool grouped = text[argument_start] == '{';
            bool has_script = !grouped;
            if (grouped) {
                size_t close = 0u;
                if (depth < TRYTR_DEFAULT_MATH_SCRIPT_DEPTH &&
                    math_group_end(text, end, argument_start, &close)) {
                    argument_start++;
                    argument_end = close;
                    has_script = true;
                }
            } else {
                argument_end += utf8_scalar_width((unsigned char)text[argument_start]);
            }
            if (has_script) {
                const char *shift = value == '_' ? "sub" : "super";
                if (!buffer_format(buffer,
                                   "<tspan baseline-shift=\"%s\" font-size=\"70%%\">",
                                   shift)) return false;
                size_t inner = argument_start;
                bool ok = grouped
                    ? append_math_text(buffer, text, &inner, argument_end, depth + 1u)
                    : append_xml_text(buffer, text + argument_start,
                                      argument_end - argument_start);
                if (!ok || !buffer_append(buffer, "</tspan>", 8u)) return false;
                *cursor = grouped ? argument_end + 1u : argument_end;
                continue;
            }
            /* Preserve malformed or over-deep script groups as source text. */
        }
        const size_t width = utf8_scalar_width((unsigned char)value);
        if (!append_xml_text(buffer, text + start, width)) return false;
        *cursor += width;
    }
    return true;
}

static bool render_text(SvgState *state, const TrytrDocumentElement *element)
{
    const char *family = "sans-serif";
    int64_t font_size;
    int64_t line_height;
    text_metrics(element, &font_size, &line_height);
    const char *weight = "400";
    const char *style = "normal";
    if (element->kind == TRYTR_ELEMENT_HEADING) {
        weight = "700";
    } else if (element->kind == TRYTR_ELEMENT_MATH) {
        family = "serif";
        style = "italic";
    }
    if (element->text == NULL && element->text_length != 0u) return false;
    const char *text = element->text == NULL ? "" : element->text;
    TextLineIterator iterator = text_line_iterator(element, font_size);
    TextLine line;
    int64_t maximum_width = 0;
    while (text_line_next(&iterator, &line)) {
        if (line.width > maximum_width) maximum_width = line.width;
    }
    const int64_t alignment_width = element->has_text_width
        ? element->text_width : maximum_width;
    int64_t anchor_x = 16;
    const char *anchor = NULL;
    if (element->has_text_alignment &&
        element->text_alignment == TRYTR_TEXT_ALIGN_CENTER) {
        anchor_x += alignment_width / 2;
        anchor = "middle";
    } else if (element->has_text_alignment &&
               element->text_alignment == TRYTR_TEXT_ALIGN_RIGHT) {
        anchor_x += alignment_width;
        anchor = "end";
    }
    if (!buffer_format(&state->output,
                       "<text x=\"%" PRId64 "\" y=\"%" PRId64
                       "\" font-family=\"%s\" font-size=\"%" PRId64
                       "\" font-weight=\"%s\" font-style=\"%s\"",
                       anchor_x,
                       state->flow_y + font_size,
                       family, font_size, weight, style)) return false;
    if (anchor != NULL &&
        !buffer_format(&state->output, " text-anchor=\"%s\"", anchor)) return false;
    if (element->has_text_color &&
        !buffer_format(&state->output, " fill=\"#%06" PRIx32 "\"",
                       element->text_color_rgb)) return false;
    if (element->id != NULL &&
        !buffer_format(&state->output, " id=\"%s\"", element->id)) return false;
    if (!buffer_append(&state->output, ">", 1u)) return false;

    iterator = text_line_iterator(element, font_size);
    size_t line_count = 0u;
    while (text_line_next(&iterator, &line)) {
        if (!buffer_format(&state->output,
                           "<tspan x=\"%" PRId64 "\" dy=\"%" PRId64 "\">",
                           anchor_x,
                           line_count == 0u ? 0 : line_height) ||
            !(element->kind == TRYTR_ELEMENT_MATH
                  ? append_math_text(&state->output, text,
                                     &line.start, line.end, 0u)
                  : append_xml_text(&state->output,
                                   text + line.start,
                                   line.end - line.start)) ||
            !buffer_append(&state->output, "</tspan>", 8u)) return false;
        ++line_count;
    }
    if (!buffer_append(&state->output, "</text>\n", 8u)) return false;

    state->flow_y += (int64_t)line_count * line_height + 8;
    return true;
}

static bool append_shape_paint(SvgBuffer *output,
                               const TrytrDocumentElement *element,
                               bool supports_fill)
{
    if (supports_fill) {
        if (element->has_fill) {
            if (!buffer_format(output, " fill=\"#%06" PRIx32 "\"",
                               element->fill_rgb)) return false;
        } else if (!buffer_append(output, " fill=\"none\"", 12u)) {
            return false;
        }
    }
    if (element->has_stroke) {
        if (!buffer_format(output, " stroke=\"#%06" PRIx32 "\"",
                           element->stroke_rgb)) return false;
    } else if (!buffer_append(output, " stroke=\"#222\"", 14u)) {
        return false;
    }
    const int64_t stroke_width = element->has_stroke_width
        ? element->stroke_width : 1;
    return buffer_format(output, " stroke-width=\"%" PRId64 "\"", stroke_width);
}

static void skip_plot_separators(const char **cursor, const char *end)
{
    while (*cursor < end && (**cursor == ' ' || **cursor == '\t' ||
           **cursor == '\n' || **cursor == '\r' || **cursor == ',')) ++*cursor;
}

static bool read_plot_number(const char **cursor, const char *end, double *out_value)
{
    skip_plot_separators(cursor, end);
    if (*cursor >= end) return false;
    char number[128];
    size_t length = 0u;
    while (*cursor < end && **cursor != ' ' && **cursor != '\t' &&
           **cursor != '\n' && **cursor != '\r' && **cursor != ',') {
        if (length + 1u >= sizeof(number)) return false;
        number[length++] = **cursor;
        ++*cursor;
    }
    number[length] = '\0';
    char *parsed_end = NULL;
    const double value = strtod(number, &parsed_end);
    if (parsed_end == number || *parsed_end != '\0' || !isfinite(value)) return false;
    *out_value = value;
    return true;
}

static double plot_x(const TrytrDocumentElement *element, double value)
{
    const double span = element->plot_max_x - element->plot_min_x;
    if (span == 0.0) return (double)element->x + (double)element->width / 2.0;
    return (double)element->x +
           (value - element->plot_min_x) * (double)element->width / span;
}

static double plot_y(const TrytrDocumentElement *element, double value)
{
    const double span = element->plot_max_y - element->plot_min_y;
    if (span == 0.0) return (double)element->y + (double)element->height / 2.0;
    return (double)element->y + (double)element->height -
           (value - element->plot_min_y) * (double)element->height / span;
}

static TrytrMathPoint map_plot_point(const TrytrDocumentElement *element,
                                     double x, double y)
{
    return (TrytrMathPoint){
        .x = plot_x(element, x),
        .y = plot_y(element, y)
    };
}

static int plot_tick_divisions(int64_t pixels, double data_span)
{
    if (data_span == 0.0) return 0;
    return pixels >= 480 ? 8 : pixels >= 320 ? 7 : pixels >= 220 ? 6 :
                  pixels >= 160 ? 5 : pixels >= 110 ? 4 : pixels >= 72 ? 3 :
                  pixels >= 40 ? 2 : 1;
}

static void format_plot_tick(double value, char *buffer, size_t capacity)
{
    const int64_t integer = (int64_t)value;
    if ((double)integer == value) {
        (void)snprintf(buffer, capacity, "%" PRId64, integer);
        return;
    }
    (void)snprintf(buffer, capacity, "%.3f", value);
    size_t length = strlen(buffer);
    while (length != 0u && buffer[length - 1u] == '0') buffer[--length] = '\0';
    if (length != 0u && buffer[length - 1u] == '.') buffer[length - 1u] = '\0';
}

static bool render_plot_ticks(SvgBuffer *output,
                              const TrytrDocumentElement *element)
{
    const double x_span = element->plot_max_x - element->plot_min_x;
    const double y_span = element->plot_max_y - element->plot_min_y;
    const int x_divisions = plot_tick_divisions(
        element->width, x_span);
    const int y_divisions = plot_tick_divisions(
        element->height, y_span);
    const double bottom = (double)(element->y + element->height);
    const int x_tick_count = x_span == 0.0 ? 1 : x_divisions + 1;
    const int y_tick_count = y_span == 0.0 ? 1 : y_divisions + 1;
    for (int division = 0; division < x_tick_count; ++division) {
        const double fraction = x_span == 0.0 ? 0.5 :
            (double)division / (double)x_divisions;
        const double x = (double)element->x + (double)element->width * fraction;
        char label[48];
        format_plot_tick(element->plot_min_x + x_span * fraction,
                         label, sizeof(label));
        if (!buffer_format(output,
                           "<line class=\"tick\" x1=\"%.3f\" y1=\"%.3f\""
                           " x2=\"%.3f\" y2=\"%.3f\" stroke=\"#888\"/>\n"
                           "<text class=\"x-tick\" x=\"%.3f\" y=\"%.3f\""
                           " text-anchor=\"middle\" font-family=\"sans-serif\""
                           " font-size=\"11\" fill=\"#667085\">%s</text>\n",
                           x, bottom, x, bottom + 4.0, x, bottom + 17.0, label)) {
            return false;
        }
    }
    for (int division = 0; division < y_tick_count; ++division) {
        const double fraction = y_span == 0.0 ? 0.5 :
            (double)division / (double)y_divisions;
        const double y = bottom - (double)element->height * fraction;
        char label[48];
        format_plot_tick(element->plot_min_y + y_span * fraction,
                         label, sizeof(label));
        if (!buffer_format(output,
                           "<line class=\"tick\" x1=\"%.3f\" y1=\"%.3f\""
                           " x2=\"%.3f\" y2=\"%.3f\" stroke=\"#888\"/>\n"
                           "<text class=\"y-tick\" x=\"%.3f\" y=\"%.3f\""
                           " text-anchor=\"end\" font-family=\"sans-serif\""
                           " font-size=\"11\" fill=\"#667085\">%s</text>\n",
                           (double)element->x - 4.0, y,
                           (double)element->x, y,
                           (double)element->x - 7.0, y + 4.0, label)) {
            return false;
        }
    }
    return true;
}

static bool render_plot(SvgState *state, const TrytrDocumentElement *element)
{
    SvgBuffer *output = &state->output;
    if (!buffer_append(output, "<g", 2u)) return false;
    if (element->id != NULL && !buffer_format(output, " id=\"%s\"", element->id)) return false;
    if (!buffer_append(output, ">\n", 2u)) return false;
    const int64_t right = element->x + element->width;
    const int64_t bottom = element->y + element->height;
    if (element->plot_grid) {
        const int x_divisions = plot_tick_divisions(
            element->width, element->plot_max_x - element->plot_min_x);
        const int y_divisions = plot_tick_divisions(
            element->height, element->plot_max_y - element->plot_min_y);
        for (int division = 1; division < x_divisions; ++division) {
            const int64_t x = element->x + element->width * division / x_divisions;
            if (!buffer_format(output,
                               "<line class=\"grid\" x1=\"%" PRId64 "\" y1=\"%" PRId64
                               "\" x2=\"%" PRId64 "\" y2=\"%" PRId64
                               "\" stroke=\"#e5e7eb\" stroke-width=\"1\"/>\n",
                               x, element->y, x, bottom)) return false;
        }
        for (int division = 1; division < y_divisions; ++division) {
            const int64_t y = element->y + element->height * division / y_divisions;
            if (!buffer_format(output,
                               "<line class=\"grid\" x1=\"%" PRId64 "\" y1=\"%" PRId64
                               "\" x2=\"%" PRId64 "\" y2=\"%" PRId64
                               "\" stroke=\"#e5e7eb\" stroke-width=\"1\"/>\n",
                               element->x, y, right, y)) return false;
        }
    }
    if (!buffer_format(output,
                       "<line class=\"axis\" x1=\"%" PRId64 "\" y1=\"%" PRId64
                       "\" x2=\"%" PRId64 "\" y2=\"%" PRId64
                       "\" stroke=\"#888\" stroke-width=\"1\"/>\n"
                       "<line class=\"axis\" x1=\"%" PRId64 "\" y1=\"%" PRId64
                       "\" x2=\"%" PRId64 "\" y2=\"%" PRId64
                       "\" stroke=\"#888\" stroke-width=\"1\"/>\n",
                       element->x, element->y, element->x, bottom,
                       element->x, bottom, right, bottom)) return false;
    if (!render_plot_ticks(output, element)) return false;
    TrytrMathPoint points[TRYTR_DEFAULT_PLOT_POINT_LIMIT];
    TrytrMathCubic segments[TRYTR_DEFAULT_PLOT_POINT_LIMIT - 1u];
    const char *cursor = element->text;
    const char *end = cursor + element->text_length;
    size_t point_count = 0u;
    while (cursor < end && point_count < TRYTR_DEFAULT_PLOT_POINT_LIMIT) {
        double x = 0.0;
        double y = 0.0;
        if (!read_plot_number(&cursor, end, &x) ||
            !read_plot_number(&cursor, end, &y)) return false;
        points[point_count] = map_plot_point(element, x, y);
        ++point_count;
    }
    if (point_count != element->point_count || point_count < 2u) return false;
    if (!trytr_math_build_curve(points, point_count, segments)) return false;
    const uint32_t curve_color = element->has_stroke
        ? element->stroke_rgb : UINT32_C(0x2563eb);
    const int64_t curve_width = element->has_stroke_width
        ? element->stroke_width : 2;
    if (!buffer_format(output,
                       "<path d=\"M %.3f %.3f",
                       points[0].x, points[0].y)) return false;
    for (size_t index = 0u; index + 1u < point_count; ++index) {
        const TrytrMathCubic *segment = &segments[index];
        if (!buffer_format(output,
                           " C %.3f %.3f %.3f %.3f %.3f %.3f",
                           segment->control1.x, segment->control1.y,
                           segment->control2.x, segment->control2.y,
                           segment->end.x, segment->end.y)) return false;
    }
    if (!buffer_format(output,
                       "\" fill=\"none\" stroke=\"#%06" PRIx32
                       "\" stroke-width=\"%" PRId64 "\"/>\n",
                       curve_color, curve_width)) return false;
    if (element->plot_markers) {
        for (size_t index = 0u; index < point_count; ++index) {
            if (!buffer_format(output,
                               "<circle class=\"data-point\" cx=\"%.3f\" cy=\"%.3f\""
                               " r=\"3\" fill=\"#%06" PRIx32
                               "\" stroke=\"#fff\" stroke-width=\"1.5\"/>\n",
                               points[index].x, points[index].y, curve_color)) return false;
        }
    }
    return buffer_append(output, "</g>\n", 5u);
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
    if (element->kind == TRYTR_ELEMENT_RECT ||
        element->kind == TRYTR_ELEMENT_ROUNDRECT) {
        if (element->id != NULL &&
            !buffer_format(&state->output, "<rect id=\"%s\"", element->id)) return false;
        if (element->id == NULL && !buffer_append(&state->output, "<rect", 5u)) return false;
        if (element->kind == TRYTR_ELEMENT_ROUNDRECT) {
            if (!buffer_format(&state->output,
                               " x=\"%" PRId64 "\" y=\"%" PRId64
                               "\" width=\"%" PRId64 "\" height=\"%" PRId64
                               "\" rx=\"%" PRId64 "\" ry=\"%" PRId64 "\"",
                               element->x, element->y, element->width, element->height,
                               element->radius, element->radius)) return false;
        } else if (!buffer_format(&state->output,
                                  " x=\"%" PRId64 "\" y=\"%" PRId64
                                  "\" width=\"%" PRId64 "\" height=\"%" PRId64 "\"",
                                  element->x, element->y, element->width, element->height)) {
            return false;
        }
        return append_shape_paint(&state->output, element, true) &&
               buffer_append(&state->output, "/>\n", 3u);
    }
    if (element->kind == TRYTR_ELEMENT_LINE) {
        if (element->id != NULL &&
            !buffer_format(&state->output, "<line id=\"%s\"", element->id)) return false;
        if (element->id == NULL && !buffer_append(&state->output, "<line", 5u)) return false;
        return buffer_format(&state->output,
                             " x1=\"%" PRId64 "\" y1=\"%" PRId64
                             "\" x2=\"%" PRId64 "\" y2=\"%" PRId64 "\"",
                             element->x, element->y, element->x2, element->y2) &&
               append_shape_paint(&state->output, element, false) &&
               buffer_append(&state->output, "/>\n", 3u);
    }
    if (element->kind == TRYTR_ELEMENT_CIRCLE) {
        if (element->id != NULL &&
            !buffer_format(&state->output, "<circle id=\"%s\"", element->id)) return false;
        if (element->id == NULL && !buffer_append(&state->output, "<circle", 7u)) return false;
        return buffer_format(&state->output,
                             " cx=\"%" PRId64 "\" cy=\"%" PRId64
                             "\" r=\"%" PRId64 "\"",
                             element->x, element->y, element->radius) &&
               append_shape_paint(&state->output, element, true) &&
               buffer_append(&state->output, "/>\n", 3u);
    }
    if (element->kind == TRYTR_ELEMENT_ELLIPSE) {
        if (element->id != NULL &&
            !buffer_format(&state->output, "<ellipse id=\"%s\"", element->id)) return false;
        if (element->id == NULL && !buffer_append(&state->output, "<ellipse", 8u)) return false;
        return buffer_format(&state->output,
                             " cx=\"%" PRId64 "\" cy=\"%" PRId64
                             "\" rx=\"%" PRId64 "\" ry=\"%" PRId64 "\"",
                             element->x, element->y, element->radius_x, element->radius_y) &&
               append_shape_paint(&state->output, element, true) &&
               buffer_append(&state->output, "/>\n", 3u);
    }
    if (element->kind == TRYTR_ELEMENT_PATH) {
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
        return buffer_append(&state->output, "\"", 1u) &&
               append_shape_paint(&state->output, element, true) &&
               buffer_append(&state->output, "/>\n", 3u);
    }
    if (element->kind == TRYTR_ELEMENT_POLYGON) {
        if (element->id != NULL &&
            !buffer_format(&state->output, "<polygon id=\"%s\" points=\"", element->id)) return false;
        if (element->id == NULL &&
            !buffer_append(&state->output, "<polygon points=\"", 17u)) return false;
        for (size_t index = 0u; index < element->text_length; ++index) {
            const char value = element->text[index];
            if (value == '\n' || value == '\r' || value == '\t') {
                if (!buffer_append(&state->output, " ", 1u)) return false;
            } else if (!buffer_append(&state->output, &value, 1u)) {
                return false;
            }
        }
        return buffer_append(&state->output, "\"", 1u) &&
               append_shape_paint(&state->output, element, true) &&
               buffer_append(&state->output, "/>\n", 3u);
    }
    if (element->kind == TRYTR_ELEMENT_PLOT) return render_plot(state, element);
    return render_text(state, element);
}

static TrytrStatus append_svg_header(SvgState *state)
{
    int64_t min_x = state->have_bounds ? state->min_x - 16 : 0;
    int64_t min_y = state->have_bounds ? state->min_y - 16 : 0;
    int64_t width = state->have_bounds ? state->max_x - state->min_x + 32 : 1;
    int64_t height = state->have_bounds ? state->max_y - state->min_y + 32 : 1;
    const char *overflow = "";
    if (state->has_fixed_canvas) {
        min_x = 0;
        min_y = 0;
        width = state->canvas_width;
        height = state->canvas_height;
        overflow = " overflow=\"hidden\"";
    }
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (!buffer_format(&state->output,
                       "<svg xmlns=\"http://www.w3.org/2000/svg\" role=\"img\" "
                       "width=\"%" PRId64 "\" height=\"%" PRId64
                       "\" viewBox=\"%" PRId64 " %" PRId64 " %" PRId64 " %" PRId64
                       "\"%s><title>trytr document</title>\n",
                       width, height, min_x, min_y, width, height, overflow)) {
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
    state.has_fixed_canvas = trytr_document_get_canvas_size(
        document, &state.canvas_width, &state.canvas_height);
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
