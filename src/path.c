#include "path.h"

#include "limits.h"

#include <stdbool.h>
#include <stdint.h>

static bool is_space(char value)
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

static bool is_command(char value)
{
    return value == 'M' || value == 'L' || value == 'H' || value == 'V' ||
           value == 'C' || value == 'Q' || value == 'Z';
}

static void skip_space(const char *data, size_t length, size_t *cursor)
{
    while (*cursor < length && is_space(data[*cursor])) ++*cursor;
}

static bool read_integer(const char *data, size_t length, size_t *cursor,
                         bool require_separator, int64_t *out_value,
                         TrytrPathStatus *out_failure_status,
                         size_t *out_error_offset,
                         size_t *out_value_offset)
{
    *out_failure_status = TRYTR_PATH_INVALID;
    const size_t before_separators = *cursor;
    skip_space(data, length, cursor);
    bool had_separator = *cursor != before_separators;
    if (*cursor < length && data[*cursor] == ',') {
        if (!require_separator) {
            *out_error_offset = *cursor;
            return false;
        }
        had_separator = true;
        ++*cursor;
        skip_space(data, length, cursor);
        if (*cursor < length && data[*cursor] == ',') {
            *out_error_offset = *cursor;
            return false;
        }
    }
    if (require_separator && !had_separator) {
        *out_error_offset = before_separators;
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
    uint64_t magnitude = 0u;
    const uint64_t maximum = (uint64_t)INT64_MAX + (negative ? 1u : 0u);
    while (*cursor < length && data[*cursor] >= '0' && data[*cursor] <= '9') {
        const uint64_t digit = (uint64_t)(data[*cursor] - '0');
        if (magnitude > (maximum - digit) / 10u) {
            *out_error_offset = start;
            *out_failure_status = TRYTR_PATH_COORDINATE_RANGE;
            return false;
        }
        magnitude = magnitude * 10u + digit;
        ++*cursor;
    }
    if (*cursor == digits_start) {
        *out_error_offset = start;
        return false;
    }
    *out_value = negative
        ? (magnitude == (uint64_t)INT64_MAX + 1u ? INT64_MIN : -(int64_t)magnitude)
        : (int64_t)magnitude;
    return true;
}

static void include_point(TrytrPathAnalysis *analysis, int64_t x, int64_t y, bool *has_point)
{
    if (!*has_point) {
        analysis->min_x = x;
        analysis->max_x = x;
        analysis->min_y = y;
        analysis->max_y = y;
        *has_point = true;
        return;
    }
    if (x < analysis->min_x) analysis->min_x = x;
    if (x > analysis->max_x) analysis->max_x = x;
    if (y < analysis->min_y) analysis->min_y = y;
    if (y > analysis->max_y) analysis->max_y = y;
}

static bool coordinate_in_range(int64_t value)
{
    return value >= -TRYTR_DEFAULT_COORDINATE_LIMIT &&
           value <= TRYTR_DEFAULT_COORDINATE_LIMIT;
}

TrytrPathStatus trytr_path_analyze(
    const char *data,
    size_t length,
    TrytrPathAnalysis *out_analysis,
    size_t *out_error_offset
)
{
    if (out_analysis == NULL || out_error_offset == NULL ||
        (data == NULL && length != 0u)) return TRYTR_PATH_INVALID;
    *out_analysis = (TrytrPathAnalysis){0};
    *out_error_offset = 0u;
    if (length > TRYTR_DEFAULT_PATH_BYTE_LIMIT) return TRYTR_PATH_LIMIT_EXCEEDED;
    if (length == 0u) return TRYTR_PATH_INVALID;

    size_t cursor = 0u;
    size_t segments = 0u;
    bool has_point = false;
    bool has_current = false;
    bool has_subpath = false;
    int64_t current_x = 0;
    int64_t current_y = 0;
    int64_t subpath_x = 0;
    int64_t subpath_y = 0;

    while (cursor < length) {
        skip_space(data, length, &cursor);
        if (cursor == length) break;
        const size_t command_offset = cursor;
        const char command = data[cursor++];
        if (!is_command(command) || (!has_subpath && command != 'M')) {
            *out_error_offset = command_offset;
            return TRYTR_PATH_INVALID;
        }

        if (command == 'Z') {
            if (!has_current || !has_subpath) {
                *out_error_offset = command_offset;
                return TRYTR_PATH_INVALID;
            }
            current_x = subpath_x;
            current_y = subpath_y;
            include_point(out_analysis, current_x, current_y, &has_point);
        } else {
            const size_t argument_count = command == 'C' ? 6u :
                                          command == 'Q' ? 4u :
                                          command == 'M' || command == 'L' ? 2u : 1u;
            int64_t arguments[6] = {0};
            for (size_t index = 0u; index < argument_count; ++index) {
                size_t argument_offset = cursor;
                TrytrPathStatus parse_failure = TRYTR_PATH_INVALID;
                if (!read_integer(data, length, &cursor, index != 0u,
                                  &arguments[index], &parse_failure, out_error_offset,
                                  &argument_offset)) {
                    return parse_failure;
                }
                if (!coordinate_in_range(arguments[index])) {
                    *out_error_offset = argument_offset;
                    return TRYTR_PATH_COORDINATE_RANGE;
                }
            }

            switch (command) {
                case 'M':
                    current_x = arguments[0];
                    current_y = arguments[1];
                    subpath_x = current_x;
                    subpath_y = current_y;
                    has_subpath = true;
                    has_current = true;
                    include_point(out_analysis, current_x, current_y, &has_point);
                    break;
                case 'L':
                    if (!has_current) { *out_error_offset = command_offset; return TRYTR_PATH_INVALID; }
                    current_x = arguments[0]; current_y = arguments[1];
                    include_point(out_analysis, current_x, current_y, &has_point);
                    break;
                case 'H':
                    if (!has_current) { *out_error_offset = command_offset; return TRYTR_PATH_INVALID; }
                    current_x = arguments[0];
                    include_point(out_analysis, current_x, current_y, &has_point);
                    break;
                case 'V':
                    if (!has_current) { *out_error_offset = command_offset; return TRYTR_PATH_INVALID; }
                    current_y = arguments[0];
                    include_point(out_analysis, current_x, current_y, &has_point);
                    break;
                case 'C':
                    if (!has_current) { *out_error_offset = command_offset; return TRYTR_PATH_INVALID; }
                    include_point(out_analysis, arguments[0], arguments[1], &has_point);
                    include_point(out_analysis, arguments[2], arguments[3], &has_point);
                    current_x = arguments[4]; current_y = arguments[5];
                    include_point(out_analysis, current_x, current_y, &has_point);
                    break;
                case 'Q':
                    if (!has_current) { *out_error_offset = command_offset; return TRYTR_PATH_INVALID; }
                    include_point(out_analysis, arguments[0], arguments[1], &has_point);
                    current_x = arguments[2]; current_y = arguments[3];
                    include_point(out_analysis, current_x, current_y, &has_point);
                    break;
                default:
                    *out_error_offset = command_offset;
                    return TRYTR_PATH_INVALID;
            }
        }

        ++segments;
        if (segments > TRYTR_DEFAULT_PATH_SEGMENT_LIMIT) {
            *out_error_offset = command_offset;
            return TRYTR_PATH_LIMIT_EXCEEDED;
        }
    }

    if (!has_point || segments == 0u) {
        *out_error_offset = cursor;
        return TRYTR_PATH_INVALID;
    }
    out_analysis->segment_count = segments;
    return TRYTR_PATH_OK;
}
