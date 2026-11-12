#ifndef TRYTR_SRC_PATH_H
#define TRYTR_SRC_PATH_H

#include <stddef.h>
#include <stdint.h>

typedef enum TrytrPathStatus {
    TRYTR_PATH_OK = 0,
    TRYTR_PATH_INVALID,
    TRYTR_PATH_LIMIT_EXCEEDED,
    TRYTR_PATH_COORDINATE_RANGE
} TrytrPathStatus;

typedef struct TrytrPathAnalysis {
    size_t segment_count;
    int64_t min_x;
    int64_t min_y;
    int64_t max_x;
    int64_t max_y;
} TrytrPathAnalysis;

TrytrPathStatus trytr_path_analyze(
    const char *data,
    size_t length,
    TrytrPathAnalysis *out_analysis,
    size_t *out_error_offset
);

#endif
