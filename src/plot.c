#include "plot.h"

#include "limits.h"

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define PLOT_NUMBER_TEXT_LIMIT 128u

static bool is_space(char value)
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

static bool read_coordinate(const char *data, size_t length, size_t *cursor,
                            bool require_separator, double *out_value,
                            size_t *out_error_offset, bool *out_range_error)
{
    const size_t before_separator = *cursor;
    while (*cursor < length && is_space(data[*cursor])) ++*cursor;
    bool separated = *cursor != before_separator;
    if (*cursor < length && data[*cursor] == ',') {
        if (!require_separator) {
            *out_error_offset = *cursor;
            return false;
        }
        separated = true;
        ++*cursor;
        while (*cursor < length && is_space(data[*cursor])) ++*cursor;
        if (*cursor < length && data[*cursor] == ',') {
            *out_error_offset = *cursor;
            return false;
        }
    }
    if (require_separator && !separated) {
        *out_error_offset = before_separator;
        return false;
    }
    if (*cursor >= length) {
        *out_error_offset = *cursor;
        return false;
    }

    const size_t start = *cursor;
    if (data[*cursor] == '+' || data[*cursor] == '-') ++*cursor;
    bool has_digit = false;
    while (*cursor < length && data[*cursor] >= '0' && data[*cursor] <= '9') {
        has_digit = true;
        ++*cursor;
    }
    if (*cursor < length && data[*cursor] == '.') {
        ++*cursor;
        while (*cursor < length && data[*cursor] >= '0' && data[*cursor] <= '9') {
            has_digit = true;
            ++*cursor;
        }
    }
    if (!has_digit) {
        *out_error_offset = start;
        return false;
    }
    if (*cursor < length && (data[*cursor] == 'e' || data[*cursor] == 'E')) {
        ++*cursor;
        if (*cursor < length && (data[*cursor] == '+' || data[*cursor] == '-')) ++*cursor;
        const size_t exponent_start = *cursor;
        while (*cursor < length && data[*cursor] >= '0' && data[*cursor] <= '9') ++*cursor;
        if (*cursor == exponent_start) {
            *out_error_offset = start;
            return false;
        }
    }

    const size_t token_length = *cursor - start;
    if (token_length >= PLOT_NUMBER_TEXT_LIMIT) {
        *out_error_offset = start;
        *out_range_error = true;
        return false;
    }
    char number[PLOT_NUMBER_TEXT_LIMIT];
    memcpy(number, data + start, token_length);
    number[token_length] = '\0';
    errno = 0;
    char *end = NULL;
    const double value = strtod(number, &end);
    if (end == number || *end != '\0') {
        *out_error_offset = start;
        return false;
    }
    if (errno == ERANGE || !isfinite(value) ||
        fabs(value) > (double)TRYTR_DEFAULT_COORDINATE_LIMIT) {
        *out_error_offset = start;
        *out_range_error = true;
        return false;
    }
    *out_value = value;
    return true;
}

static void include_point(TrytrPlotAnalysis *analysis, double x, double y,
                          bool first)
{
    if (first) {
        analysis->min_x = analysis->max_x = x;
        analysis->min_y = analysis->max_y = y;
    } else {
        if (x < analysis->min_x) analysis->min_x = x;
        if (x > analysis->max_x) analysis->max_x = x;
        if (y < analysis->min_y) analysis->min_y = y;
        if (y > analysis->max_y) analysis->max_y = y;
    }
}

TrytrPlotStatus trytr_plot_analyze(const char *data, size_t length,
                                  TrytrPlotAnalysis *out_analysis,
                                  size_t *out_error_offset)
{
    if (out_analysis == NULL || out_error_offset == NULL ||
        (data == NULL && length != 0u)) return TRYTR_PLOT_INVALID;
    *out_analysis = (TrytrPlotAnalysis){0};
    *out_error_offset = 0u;
    if (length > TRYTR_DEFAULT_PLOT_BYTE_LIMIT) return TRYTR_PLOT_LIMIT_EXCEEDED;
    if (length == 0u) return TRYTR_PLOT_INVALID;

    size_t cursor = 0u;
    size_t coordinate_count = 0u;
    double x = 0.0;
    while (cursor < length) {
        size_t trailing_space = cursor;
        while (trailing_space < length && is_space(data[trailing_space])) ++trailing_space;
        if (trailing_space == length) break;
        double coordinate = 0.0;
        bool range_error = false;
        if (!read_coordinate(data, length, &cursor, coordinate_count != 0u,
                             &coordinate, out_error_offset, &range_error)) {
            return range_error ? TRYTR_PLOT_COORDINATE_RANGE : TRYTR_PLOT_INVALID;
        }
        if ((coordinate_count & 1u) == 0u) {
            x = coordinate;
        } else {
            include_point(out_analysis, x, coordinate,
                          out_analysis->point_count == 0u);
            ++out_analysis->point_count;
            if (out_analysis->point_count > TRYTR_DEFAULT_PLOT_POINT_LIMIT) {
                *out_error_offset = cursor;
                return TRYTR_PLOT_LIMIT_EXCEEDED;
            }
        }
        ++coordinate_count;
    }

    if (coordinate_count < 4u || (coordinate_count & 1u) != 0u) {
        *out_error_offset = cursor;
        return TRYTR_PLOT_INVALID;
    }
    return TRYTR_PLOT_OK;
}
