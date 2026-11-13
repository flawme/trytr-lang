#include "text_render_internal.h"
#include "math_1.h"
#include "plot.h"

#include <stdlib.h>
#include <string.h>

enum {
    TEXT_SUBCELLS_X = 2,
    TEXT_SUBCELLS_Y = 3
};

static int map_x(const TextState *state, int64_t x)
{
    const int64_t span = state->max_x - state->min_x;
    if (span <= 0) return (int)(state->columns / 2u);
    int64_t position = (x - state->min_x) * (int64_t)(state->columns - 1u) / span;
    if (position < 0) position = 0;
    if (position >= (int64_t)state->columns) position = (int64_t)state->columns - 1;
    return (int)position;
}

static int map_y(const TextState *state, int64_t y)
{
    const int64_t span = state->max_y - state->min_y;
    if (span <= 0) return (int)(state->rows / 2u);
    int64_t position = (y - state->min_y) * (int64_t)(state->rows - 1u) / span;
    if (position < 0) position = 0;
    if (position >= (int64_t)state->rows) position = (int64_t)state->rows - 1;
    return (int)(state->rows - 1u) - (int)position;
}

static double map_cell_x(const TextState *state, double x)
{
    const int64_t span = state->max_x - state->min_x;
    if (span <= 0) return (double)(state->columns - 1u) / 2.0;
    double position = (x - (double)state->min_x) *
                      (double)(state->columns - 1u) / (double)span;
    if (position < 0.0) position = 0.0;
    if (position > (double)(state->columns - 1u))
        position = (double)(state->columns - 1u);
    return position;
}

static double map_cell_y(const TextState *state, double y)
{
    const int64_t span = state->max_y - state->min_y;
    if (span <= 0) return (double)(state->rows - 1u) / 2.0;
    double position = (y - (double)state->min_y) *
                      (double)(state->rows - 1u) / (double)span;
    if (position < 0.0) position = 0.0;
    if (position > (double)(state->rows - 1u))
        position = (double)(state->rows - 1u);
    return (double)(state->rows - 1u) - position;
}

static TrytrMathPoint map_subcell_point(const TextState *state, TrytrMathPoint point)
{
    return (TrytrMathPoint){
        map_cell_x(state, point.x) * TEXT_SUBCELLS_X,
        map_cell_y(state, point.y) * TEXT_SUBCELLS_Y
    };
}

static void put_cell(TextState *state, int x, int y, uint32_t value)
{
    if (x < 0 || y < 0 || (size_t)x >= state->columns || (size_t)y >= state->rows) return;
    uint32_t *cell = &state->cells[y][x];
    if (*cell == '*') return;
    if (value == '*') *cell = value;
    else if (*cell == ' ') *cell = value;
    else if (*cell != value && *cell != '+') *cell = '+';
}

static void mark_subcell(TextState *state, int x, int y)
{
    if (x < 0 || y < 0 ||
        (size_t)x >= state->columns * TEXT_SUBCELLS_X ||
        (size_t)y >= state->rows * TEXT_SUBCELLS_Y) return;
    const size_t column = (size_t)x / TEXT_SUBCELLS_X;
    const size_t row = (size_t)y / TEXT_SUBCELLS_Y;
    const unsigned int local_x = (unsigned int)x % TEXT_SUBCELLS_X;
    const unsigned int local_y = (unsigned int)y % TEXT_SUBCELLS_Y;
    const unsigned int bit = local_y * TEXT_SUBCELLS_X + local_x;
    state->block_masks[row][column] |= (uint8_t)(1u << bit);
}

static void draw_line_cell(int x, int y, void *user_data)
{
    mark_subcell(user_data, x, y);
}

static void draw_subcell_line(TextState *state, double x0, double y0,
                              double x1, double y1)
{
    (void)trytr_math_trace_line((TrytrMathPoint){x0, y0},
                                (TrytrMathPoint){x1, y1},
                                draw_line_cell, state);
}

static void draw_math_line(TextState *state, double x0, double y0,
                           double x1, double y1)
{
    draw_subcell_line(state, x0 * TEXT_SUBCELLS_X, y0 * TEXT_SUBCELLS_Y,
                      x1 * TEXT_SUBCELLS_X, y1 * TEXT_SUBCELLS_Y);
}

static void draw_line_pixels(TextState *state, int x0, int y0, int x1, int y1)
{
    draw_math_line(state, (double)x0, (double)y0,
                   (double)x1, (double)y1);
}

static uint32_t block_glyph(uint8_t mask)
{
    /* Row-major 2x3 masks to Unicode Block Sextants. Sextant numbers are
     * row-major too: 1/2 on top, 3/4 in the middle, 5/6 on the bottom. */
    static const uint32_t glyphs[64] = {
        UINT32_C(0x0020), UINT32_C(0x1fb00), UINT32_C(0x1fb01), UINT32_C(0x1fb02),
        UINT32_C(0x1fb03), UINT32_C(0x1fb04), UINT32_C(0x1fb05), UINT32_C(0x1fb06),
        UINT32_C(0x1fb07), UINT32_C(0x1fb08), UINT32_C(0x1fb09), UINT32_C(0x1fb0a),
        UINT32_C(0x1fb0b), UINT32_C(0x1fb0c), UINT32_C(0x1fb0d), UINT32_C(0x1fb0e),
        UINT32_C(0x1fb0f), UINT32_C(0x1fb10), UINT32_C(0x1fb11), UINT32_C(0x1fb12),
        UINT32_C(0x1fb13), UINT32_C(0x258c), UINT32_C(0x1fb14), UINT32_C(0x1fb15),
        UINT32_C(0x1fb16), UINT32_C(0x1fb17), UINT32_C(0x1fb18), UINT32_C(0x1fb19),
        UINT32_C(0x1fb1a), UINT32_C(0x1fb1b), UINT32_C(0x1fb1c), UINT32_C(0x1fb1d),
        UINT32_C(0x1fb1e), UINT32_C(0x1fb1f), UINT32_C(0x1fb20), UINT32_C(0x1fb21),
        UINT32_C(0x1fb22), UINT32_C(0x1fb23), UINT32_C(0x1fb24), UINT32_C(0x1fb25),
        UINT32_C(0x1fb26), UINT32_C(0x1fb27), UINT32_C(0x2590), UINT32_C(0x1fb28),
        UINT32_C(0x1fb29), UINT32_C(0x1fb2a), UINT32_C(0x1fb2b), UINT32_C(0x1fb2c),
        UINT32_C(0x1fb2d), UINT32_C(0x1fb2e), UINT32_C(0x1fb2f), UINT32_C(0x1fb30),
        UINT32_C(0x1fb31), UINT32_C(0x1fb32), UINT32_C(0x1fb33), UINT32_C(0x1fb34),
        UINT32_C(0x1fb35), UINT32_C(0x1fb36), UINT32_C(0x1fb37), UINT32_C(0x1fb38),
        UINT32_C(0x1fb39), UINT32_C(0x1fb3a), UINT32_C(0x1fb3b), UINT32_C(0x2588)
    };
    return glyphs[mask & UINT8_C(0x3f)];
}

static bool append_codepoint(TextBuffer *buffer, uint32_t codepoint)
{
    char encoded[4];
    size_t length = 0u;
    if (codepoint <= UINT32_C(0x7f)) {
        encoded[0] = (char)codepoint;
        length = 1u;
    } else if (codepoint <= UINT32_C(0x7ff)) {
        encoded[0] = (char)(0xc0u | (codepoint >> 6u));
        encoded[1] = (char)(0x80u | (codepoint & 0x3fu));
        length = 2u;
    } else if (codepoint <= UINT32_C(0xffff) &&
               !(codepoint >= UINT32_C(0xd800) && codepoint <= UINT32_C(0xdfff))) {
        encoded[0] = (char)(0xe0u | (codepoint >> 12u));
        encoded[1] = (char)(0x80u | ((codepoint >> 6u) & 0x3fu));
        encoded[2] = (char)(0x80u | (codepoint & 0x3fu));
        length = 3u;
    } else if (codepoint <= UINT32_C(0x10ffff)) {
        encoded[0] = (char)(0xf0u | (codepoint >> 18u));
        encoded[1] = (char)(0x80u | ((codepoint >> 12u) & 0x3fu));
        encoded[2] = (char)(0x80u | ((codepoint >> 6u) & 0x3fu));
        encoded[3] = (char)(0x80u | (codepoint & 0x3fu));
        length = 4u;
    } else {
        return false;
    }
    return trytr_text_buffer_append(buffer, encoded, length);
}

static void draw_segment(TextState *state, int64_t x1, int64_t y1,
                         int64_t x2, int64_t y2)
{
    draw_line_pixels(state, map_x(state, x1), map_y(state, y1),
                     map_x(state, x2), map_y(state, y2));
}

static void draw_rectangle(TextState *state, int64_t x, int64_t y,
                           int64_t width, int64_t height)
{
    const int left = map_x(state, x);
    const int right = map_x(state, x + width);
    const int top = map_y(state, y);
    const int bottom = map_y(state, y + height);
    for (int column = left; column <= right; ++column) {
        put_cell(state, column, top, column == left || column == right ? '+' : '-');
        put_cell(state, column, bottom, column == left || column == right ? '+' : '-');
    }
    for (int row = top; row <= bottom; ++row) {
        put_cell(state, left, row, '+');
        put_cell(state, right, row, '+');
        if (row != top && row != bottom) {
            put_cell(state, left, row, '|');
            put_cell(state, right, row, '|');
        }
    }
}

static uint64_t integer_sqrt(uint64_t value)
{
    uint64_t low = 0u;
    uint64_t high = value < UINT32_MAX ? value + 1u : UINT32_MAX;
    while (low + 1u < high) {
        const uint64_t middle = low + (high - low) / 2u;
        if (middle <= value / middle) low = middle;
        else high = middle;
    }
    return low;
}

static void draw_ellipse(TextState *state, int64_t cx, int64_t cy,
                        int64_t rx, int64_t ry)
{
    const int px = map_x(state, cx);
    const int py = map_y(state, cy);
    const int pixel_rx = abs(map_x(state, cx + rx) - map_x(state, cx - rx));
    const int pixel_ry = abs(map_y(state, cy + ry) - map_y(state, cy - ry));
    if (pixel_rx == 0) {
        for (int offset = -pixel_ry / 2; offset <= pixel_ry / 2; ++offset)
            put_cell(state, px, py + offset, '*');
        return;
    }
    if (pixel_ry == 0) {
        for (int offset = -pixel_rx / 2; offset <= pixel_rx / 2; ++offset)
            put_cell(state, px + offset, py, '*');
        return;
    }
    const int half_rx = pixel_rx / 2;
    const int half_ry = pixel_ry / 2;
    if (half_rx == 0) {
        for (int offset = -half_ry; offset <= half_ry; ++offset)
            put_cell(state, px, py + offset, '*');
        return;
    }
    if (half_ry == 0) {
        for (int offset = -half_rx; offset <= half_rx; ++offset)
            put_cell(state, px + offset, py, '*');
        return;
    }
    const uint64_t rx_square = (uint64_t)half_rx * (uint64_t)half_rx;
    for (int offset_x = -half_rx; offset_x <= half_rx; ++offset_x) {
        const uint64_t x_square = (uint64_t)(offset_x * offset_x);
        const uint64_t numerator = rx_square - x_square;
        const uint64_t y_square = (uint64_t)half_ry * (uint64_t)half_ry * numerator / rx_square;
        const int offset_y = (int)integer_sqrt(y_square);
        put_cell(state, px + offset_x, py + offset_y, '*');
        put_cell(state, px + offset_x, py - offset_y, '*');
    }
}

static void skip_separators(const char **cursor, const char *end)
{
    while (*cursor < end && (**cursor == ' ' || **cursor == '\t' ||
           **cursor == '\n' || **cursor == '\r' || **cursor == ',')) ++*cursor;
}

static bool read_integer(const char **cursor, const char *end, int64_t *out_value)
{
    skip_separators(cursor, end);
    if (*cursor >= end) return false;
    char *parsed_end = NULL;
    const long long value = strtoll(*cursor, &parsed_end, 10);
    if (parsed_end == *cursor || parsed_end > end) return false;
    *cursor = parsed_end;
    *out_value = (int64_t)value;
    return true;
}

static void draw_polygon(TextState *state, const TrytrDocumentElement *element)
{
    const char *cursor = element->text;
    const char *end = cursor + element->text_length;
    int64_t first_x = 0;
    int64_t first_y = 0;
    int64_t previous_x = 0;
    int64_t previous_y = 0;
    bool first = true;
    while (cursor < end) {
        int64_t x = 0;
        int64_t y = 0;
        if (!read_integer(&cursor, end, &x) || !read_integer(&cursor, end, &y)) break;
        x += state->group_x;
        y += state->group_y;
        if (first) {
            first_x = x;
            first_y = y;
            first = false;
        } else {
            draw_segment(state, previous_x, previous_y, x, y);
        }
        previous_x = x;
        previous_y = y;
    }
    if (!first) draw_segment(state, previous_x, previous_y, first_x, first_y);
}

static double plot_coordinate_x(const TrytrDocumentElement *element, int64_t value)
{
    const int64_t span = element->max_x - element->min_x;
    if (span == 0) return (double)element->x + (double)element->width / 2.0;
    return (double)element->x +
           (double)(value - element->min_x) * (double)element->width / (double)span;
}

static double plot_coordinate_y(const TrytrDocumentElement *element, int64_t value)
{
    const int64_t span = element->max_y - element->min_y;
    if (span == 0) return (double)element->y + (double)element->height / 2.0;
    return (double)element->y +
           (double)(value - element->min_y) * (double)element->height / (double)span;
}

static void draw_flat_curve_segment(TrytrMathPoint start, TrytrMathPoint end,
                                    void *user_data)
{
    TextState *state = user_data;
    draw_subcell_line(state, start.x, start.y, end.x, end.y);
}

static void draw_text_curve(TextState *state, TrytrMathCubic curve)
{
    curve.start = map_subcell_point(state, curve.start);
    curve.control1 = map_subcell_point(state, curve.control1);
    curve.control2 = map_subcell_point(state, curve.control2);
    curve.end = map_subcell_point(state, curve.end);
    (void)trytr_math_flatten_cubic(&curve, 0.2,
                                   draw_flat_curve_segment, state);
}

static void draw_plot(TextState *state, const TrytrDocumentElement *element)
{
    const int64_t left = element->x + state->group_x;
    const int64_t top = element->y + state->group_y;
    const int64_t right = left + element->width;
    const int64_t upper = top + element->height;
    draw_segment(state, left, upper, left, top);
    draw_segment(state, left, top, right, top);

    TrytrMathPoint points[TRYTR_DEFAULT_PLOT_POINT_LIMIT];
    TrytrMathCubic segments[TRYTR_DEFAULT_PLOT_POINT_LIMIT - 1u];
    const char *cursor = element->text;
    const char *end = cursor + element->text_length;
    size_t point_count = 0u;
    while (cursor < end && point_count < TRYTR_DEFAULT_PLOT_POINT_LIMIT) {
        int64_t x = 0;
        int64_t y = 0;
        if (!read_integer(&cursor, end, &x) || !read_integer(&cursor, end, &y)) return;
        points[point_count].x = plot_coordinate_x(element, x) + (double)state->group_x;
        points[point_count].y = plot_coordinate_y(element, y) + (double)state->group_y;
        ++point_count;
    }
    if (point_count != element->point_count || point_count < 2u) return;
    if (!trytr_math_build_curve(points, point_count, segments)) return;
    for (size_t index = 0u; index + 1u < point_count; ++index) {
        draw_text_curve(state, segments[index]);
    }
}

static bool path_read_integer(const char **cursor, const char *end, int64_t *out_value)
{
    return read_integer(cursor, end, out_value);
}

static void draw_path(TextState *state, const TrytrDocumentElement *element)
{
    const char *cursor = element->text;
    const char *end = cursor + element->text_length;
    int64_t current_x = 0;
    int64_t current_y = 0;
    int64_t start_x = 0;
    int64_t start_y = 0;
    while (cursor < end) {
        skip_separators(&cursor, end);
        if (cursor >= end) break;
        const char command = *cursor++;
        int64_t args[6] = {0};
        size_t count = command == 'C' ? 6u : command == 'Q' ? 4u :
                       command == 'M' || command == 'L' ? 2u :
                       command == 'H' || command == 'V' ? 1u : 0u;
        if (command == 'Z') {
            draw_segment(state, current_x + state->group_x,
                         current_y + state->group_y,
                         start_x + state->group_x, start_y + state->group_y);
            current_x = start_x;
            current_y = start_y;
            continue;
        }
        if (count == 0u) break;
        bool valid = true;
        for (size_t index = 0u; index < count; ++index) {
            if (!path_read_integer(&cursor, end, &args[index])) { valid = false; break; }
        }
        if (!valid) break;
        const int64_t old_x = current_x;
        const int64_t old_y = current_y;
        if (command == 'M') {
            current_x = args[0]; current_y = args[1];
            start_x = current_x; start_y = current_y;
        } else if (command == 'L') {
            current_x = args[0]; current_y = args[1];
            draw_segment(state, old_x + state->group_x, old_y + state->group_y,
                         current_x + state->group_x, current_y + state->group_y);
        } else if (command == 'H') {
            current_x = args[0];
            draw_segment(state, old_x + state->group_x, old_y + state->group_y,
                         current_x + state->group_x, current_y + state->group_y);
        } else if (command == 'V') {
            current_y = args[0];
            draw_segment(state, old_x + state->group_x, old_y + state->group_y,
                         current_x + state->group_x, current_y + state->group_y);
        } else if (command == 'Q' || command == 'C') {
            const int64_t target_x = command == 'C' ? args[4] : args[2];
            const int64_t target_y = command == 'C' ? args[5] : args[3];
            TrytrMathCubic curve;
            if (command == 'C') {
                curve = (TrytrMathCubic){
                    .start = {(double)old_x + (double)state->group_x,
                              (double)old_y + (double)state->group_y},
                    .control1 = {(double)args[0] + (double)state->group_x,
                                 (double)args[1] + (double)state->group_y},
                    .control2 = {(double)args[2] + (double)state->group_x,
                                 (double)args[3] + (double)state->group_y},
                    .end = {(double)target_x + (double)state->group_x,
                            (double)target_y + (double)state->group_y}
                };
            } else {
                curve = trytr_math_quadratic_to_cubic(
                    (TrytrMathPoint){(double)old_x + (double)state->group_x,
                                     (double)old_y + (double)state->group_y},
                    (TrytrMathPoint){(double)args[0] + (double)state->group_x,
                                     (double)args[1] + (double)state->group_y},
                    (TrytrMathPoint){(double)target_x + (double)state->group_x,
                                     (double)target_y + (double)state->group_y});
            }
            draw_text_curve(state, curve);
            current_x = target_x;
            current_y = target_y;
        }
    }
}

static bool element_is_outside_canvas(const TextState *state,
                                      const TrytrDocumentElement *element)
{
    int64_t x1 = 0;
    int64_t y1 = 0;
    int64_t x2 = 0;
    int64_t y2 = 0;
    switch (element->kind) {
        case TRYTR_ELEMENT_RECT:
        case TRYTR_ELEMENT_ROUNDRECT:
            x1 = element->x + state->group_x;
            y1 = element->y + state->group_y;
            x2 = x1 + element->width;
            y2 = y1 + element->height;
            break;
        case TRYTR_ELEMENT_LINE:
            x1 = element->x + state->group_x;
            y1 = element->y + state->group_y;
            x2 = element->x2 + state->group_x;
            y2 = element->y2 + state->group_y;
            break;
        case TRYTR_ELEMENT_CIRCLE:
            x1 = element->x - element->radius + state->group_x;
            y1 = element->y - element->radius + state->group_y;
            x2 = element->x + element->radius + state->group_x;
            y2 = element->y + element->radius + state->group_y;
            break;
        case TRYTR_ELEMENT_ELLIPSE:
            x1 = element->x - element->radius_x + state->group_x;
            y1 = element->y - element->radius_y + state->group_y;
            x2 = element->x + element->radius_x + state->group_x;
            y2 = element->y + element->radius_y + state->group_y;
            break;
        case TRYTR_ELEMENT_PATH:
        case TRYTR_ELEMENT_POLYGON:
            x1 = element->min_x + state->group_x;
            y1 = element->min_y + state->group_y;
            x2 = element->max_x + state->group_x;
            y2 = element->max_y + state->group_y;
            break;
        case TRYTR_ELEMENT_PLOT:
            x1 = element->x + state->group_x;
            y1 = element->y + state->group_y;
            x2 = x1 + element->width;
            y2 = y1 + element->height;
            break;
        default:
            return false;
    }
    if (x1 > x2) { const int64_t temporary = x1; x1 = x2; x2 = temporary; }
    if (y1 > y2) { const int64_t temporary = y1; y1 = y2; y2 = temporary; }
    return x2 < state->min_x || x1 > state->max_x ||
           y2 < state->min_y || y1 > state->max_y;
}

bool trytr_text_draw_element(const TrytrDocumentElement *element, void *user_data)
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
    if (element_is_outside_canvas(state, element)) return true;
    switch (element->kind) {
        case TRYTR_ELEMENT_RECT:
        case TRYTR_ELEMENT_ROUNDRECT:
            draw_rectangle(state, element->x + state->group_x,
                           element->y + state->group_y,
                           element->width, element->height);
            break;
        case TRYTR_ELEMENT_LINE:
            draw_segment(state, element->x + state->group_x,
                         element->y + state->group_y,
                         element->x2 + state->group_x,
                         element->y2 + state->group_y);
            break;
        case TRYTR_ELEMENT_CIRCLE:
            draw_ellipse(state, element->x + state->group_x,
                         element->y + state->group_y,
                         element->radius, element->radius);
            break;
        case TRYTR_ELEMENT_ELLIPSE:
            draw_ellipse(state, element->x + state->group_x,
                         element->y + state->group_y,
                         element->radius_x, element->radius_y);
            break;
        case TRYTR_ELEMENT_POLYGON:
            draw_polygon(state, element);
            break;
        case TRYTR_ELEMENT_PATH:
            draw_path(state, element);
            break;
        case TRYTR_ELEMENT_PLOT:
            draw_plot(state, element);
            break;
        default:
            break;
    }
    return true;
}

void trytr_text_grid_init(TextState *state, int64_t width, int64_t height)
{
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    const int64_t preferred_rows = (int64_t)TEXT_GRID_MAX_COLUMNS * height / (2 * width);
    int64_t rows = preferred_rows;
    if (rows > TEXT_GRID_MAX_ROWS) rows = TEXT_GRID_MAX_ROWS;
    if (rows < 8) rows = 8;
    int64_t columns = 2 * rows * width / height;
    if (columns > TEXT_GRID_MAX_COLUMNS) columns = TEXT_GRID_MAX_COLUMNS;
    if (columns < 20) columns = 20;
    state->rows = (size_t)rows;
    state->columns = (size_t)columns;
    for (size_t row = 0u; row < state->rows; ++row) {
        memset(state->block_masks[row], 0, state->columns);
        for (size_t column = 0u; column < state->columns; ++column)
            state->cells[row][column] = ' ';
    }
}

bool trytr_text_append_grid(TextState *state)
{
    for (size_t row = 0u; row < state->rows; ++row) {
        for (size_t column = 0u; column < state->columns; ++column) {
            if (state->block_masks[row][column] != 0u &&
                state->cells[row][column] == ' ')
                state->cells[row][column] = block_glyph(
                    state->block_masks[row][column]);
        }
        size_t last = state->columns;
        while (last > 0u && state->cells[row][last - 1u] == ' ') --last;
        for (size_t column = 0u; column < last; ++column) {
            if (!append_codepoint(&state->output, state->cells[row][column])) return false;
        }
        if (!trytr_text_buffer_append(&state->output, "\n", 1u)) return false;
    }
    return true;
}
