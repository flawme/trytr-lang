#ifndef TRYTR_SRC_POLYGON_H
#define TRYTR_SRC_POLYGON_H

#include <stddef.h>
#include <stdint.h>

typedef enum TrytrPolygonStatus {
    TRYTR_POLYGON_OK = 0,
    TRYTR_POLYGON_INVALID,
    TRYTR_POLYGON_LIMIT_EXCEEDED,
    TRYTR_POLYGON_COORDINATE_RANGE
} TrytrPolygonStatus;

typedef struct TrytrPolygonAnalysis {
    size_t point_count;
    int64_t min_x;
    int64_t min_y;
    int64_t max_x;
    int64_t max_y;
} TrytrPolygonAnalysis;

TrytrPolygonStatus trytr_polygon_analyze(
    const char *data,
    size_t length,
    TrytrPolygonAnalysis *out_analysis,
    size_t *out_error_offset
);

#endif
