#include "polygon.h"

#include "limits.h"

#include <stdbool.h>

static bool is_space(char value)
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

static void include_point(TrytrPolygonAnalysis *analysis, int64_t x, int64_t y,
                          bool first)
{
    if (first) {
        analysis->min_x = x;
        analysis->max_x = x;
        analysis->min_y = y;
        analysis->max_y = y;
        return;
    }
    if (x < analysis->min_x) analysis->min_x = x;
    if (x > analysis->max_x) analysis->max_x = x;
    if (y < analysis->min_y) analysis->min_y = y;
    if (y > analysis->max_y) analysis->max_y = y;
}

static bool read_coordinate(const char *data, size_t length, size_t *cursor,
                            bool require_separator, int64_t *out_value,
                            size_t *out_error_offset, size_t *out_value_offset,
                            TrytrPolygonStatus *out_status)
{
    *out_status = TRYTR_POLYGON_INVALID;
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
    *out_value_offset = start;
    bool negative = false;
    if (data[*cursor] == '+' || data[*cursor] == '-') {
        negative = data[*cursor] == '-';
        ++*cursor;
    }
    const size_t digits_start = *cursor;
    int64_t magnitude = 0;
    while (*cursor < length && data[*cursor] >= '0' && data[*cursor] <= '9') {
        const int64_t digit = (int64_t)(data[*cursor] - '0');
        if (magnitude > (TRYTR_DEFAULT_COORDINATE_LIMIT - digit) / 10) {
            *out_error_offset = start;
            *out_status = TRYTR_POLYGON_COORDINATE_RANGE;
            return false;
        }
        magnitude = magnitude * 10 + digit;
        ++*cursor;
    }
    if (*cursor == digits_start) {
        *out_error_offset = start;
        return false;
    }
    *out_value = negative ? -magnitude : magnitude;
    return true;
}

TrytrPolygonStatus trytr_polygon_analyze(
    const char *data,
    size_t length,
    TrytrPolygonAnalysis *out_analysis,
    size_t *out_error_offset
)
{
    if (out_analysis == NULL || out_error_offset == NULL ||
        (data == NULL && length != 0u)) return TRYTR_POLYGON_INVALID;
    *out_analysis = (TrytrPolygonAnalysis){0};
    *out_error_offset = 0u;
    if (length > TRYTR_DEFAULT_POLYGON_BYTE_LIMIT) return TRYTR_POLYGON_LIMIT_EXCEEDED;
    if (length == 0u) return TRYTR_POLYGON_INVALID;

    size_t cursor = 0u;
    size_t coordinate_count = 0u;
    int64_t x = 0;
    int64_t y = 0;
    while (cursor < length) {
        size_t trailing_space = cursor;
        while (trailing_space < length && is_space(data[trailing_space])) ++trailing_space;
        if (trailing_space == length) break;
        const bool need_separator = coordinate_count != 0u;
        int64_t coordinate = 0;
        size_t value_offset = cursor;
        TrytrPolygonStatus failure = TRYTR_POLYGON_INVALID;
        if (!read_coordinate(data, length, &cursor, need_separator, &coordinate,
                             out_error_offset, &value_offset, &failure)) {
            return failure;
        }
        if ((coordinate_count & 1u) == 0u) {
            x = coordinate;
        } else {
            y = coordinate;
            include_point(out_analysis, x, y, coordinate_count == 1u);
            ++out_analysis->point_count;
            if (out_analysis->point_count > TRYTR_DEFAULT_POLYGON_POINT_LIMIT) {
                *out_error_offset = value_offset;
                return TRYTR_POLYGON_LIMIT_EXCEEDED;
            }
        }
        ++coordinate_count;
    }

    if (coordinate_count < 6u || (coordinate_count & 1u) != 0u) {
        *out_error_offset = cursor;
        return TRYTR_POLYGON_INVALID;
    }
    return TRYTR_POLYGON_OK;
}
