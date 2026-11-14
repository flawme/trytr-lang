#ifndef TRYTR_SRC_PLOT_FORMULA_H
#define TRYTR_SRC_PLOT_FORMULA_H

#include "arena.h"
#include "plot.h"

#include <stddef.h>

typedef enum TrytrPlotFormulaStatus {
    TRYTR_PLOT_FORMULA_OK = 0,
    TRYTR_PLOT_FORMULA_INVALID,
    TRYTR_PLOT_FORMULA_DOMAIN,
    TRYTR_PLOT_FORMULA_LIMIT_EXCEEDED,
    TRYTR_PLOT_FORMULA_NO_MEMORY
} TrytrPlotFormulaStatus;

TrytrPlotFormulaStatus trytr_plot_formula_sample(
    const char *expression,
    size_t expression_length,
    double min_x,
    double max_x,
    size_t sample_count,
    TrytrArena *arena,
    char **out_points,
    size_t *out_length,
    TrytrPlotAnalysis *out_analysis,
    size_t *out_error_offset
);

#endif
