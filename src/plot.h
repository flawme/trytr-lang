#ifndef TRYTR_SRC_PLOT_H
#define TRYTR_SRC_PLOT_H

#include <stddef.h>
#include <stdint.h>

typedef enum TrytrPlotStatus {
    TRYTR_PLOT_OK = 0,
    TRYTR_PLOT_INVALID,
    TRYTR_PLOT_LIMIT_EXCEEDED,
    TRYTR_PLOT_COORDINATE_RANGE
} TrytrPlotStatus;

typedef struct TrytrPlotAnalysis {
    size_t point_count;
    double min_x;
    double min_y;
    double max_x;
    double max_y;
} TrytrPlotAnalysis;

TrytrPlotStatus trytr_plot_analyze(
    const char *data,
    size_t length,
    TrytrPlotAnalysis *out_analysis,
    size_t *out_error_offset
);

#endif
