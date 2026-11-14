#include "plot_formula.h"

#include "limits.h"
#include "trytr/math/expression.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define TRYTR_FORMULA_SAMPLE_LIMIT 1024u
#define TRYTR_FORMULA_TEXT_LIMIT 64u

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
)
{
    if (out_points != NULL) *out_points = NULL;
    if (out_length != NULL) *out_length = 0u;
    if (out_analysis != NULL) *out_analysis = (TrytrPlotAnalysis){0};
    if (out_error_offset != NULL) *out_error_offset = 0u;
    if (expression == NULL || arena == NULL || out_points == NULL ||
        out_length == NULL || out_analysis == NULL || out_error_offset == NULL ||
        expression_length == 0u) return TRYTR_PLOT_FORMULA_INVALID;
    if (expression_length > 4096u || sample_count > TRYTR_FORMULA_SAMPLE_LIMIT ||
        sample_count > SIZE_MAX / TRYTR_FORMULA_TEXT_LIMIT) {
        return TRYTR_PLOT_FORMULA_LIMIT_EXCEEDED;
    }
    if (!isfinite(min_x) || !isfinite(max_x) ||
        fabs(min_x) > (double)TRYTR_DEFAULT_COORDINATE_LIMIT ||
        fabs(max_x) > (double)TRYTR_DEFAULT_COORDINATE_LIMIT || min_x >= max_x ||
        sample_count < 2u) return TRYTR_PLOT_FORMULA_INVALID;

    TrytrMathExpression *compiled = NULL;
    const TrytrMathExpressionStatus compile_status = trytr_math_expression_compile(
        expression, expression_length, &compiled, out_error_offset);
    if (compile_status == TRYTR_MATH_EXPRESSION_NO_MEMORY) {
        return TRYTR_PLOT_FORMULA_NO_MEMORY;
    }
    if (compile_status == TRYTR_MATH_EXPRESSION_LIMIT_EXCEEDED) {
        return TRYTR_PLOT_FORMULA_LIMIT_EXCEEDED;
    }
    if (compile_status != TRYTR_MATH_EXPRESSION_OK) {
        return TRYTR_PLOT_FORMULA_INVALID;
    }

    const size_t capacity = sample_count * TRYTR_FORMULA_TEXT_LIMIT;
    char *points = trytr_arena_alloc(arena, capacity + 1u);
    if (points == NULL) {
        trytr_math_expression_destroy(compiled);
        return TRYTR_PLOT_FORMULA_NO_MEMORY;
    }
    size_t length = 0u;
    for (size_t index = 0u; index < sample_count; ++index) {
        const double fraction = (double)index / (double)(sample_count - 1u);
        const double x = index + 1u == sample_count
            ? max_x : min_x + (max_x - min_x) * fraction;
        double y = 0.0;
        if (!trytr_math_expression_evaluate(compiled, x, &y) ||
            fabs(y) > (double)TRYTR_DEFAULT_COORDINATE_LIMIT) {
            *out_error_offset = expression_length;
            trytr_math_expression_destroy(compiled);
            return TRYTR_PLOT_FORMULA_DOMAIN;
        }
        char pair[TRYTR_FORMULA_TEXT_LIMIT];
        const int written = snprintf(pair, sizeof(pair), "%s%.17g,%.17g",
                                     index == 0u ? "" : " ", x, y);
        if (written < 0 || (size_t)written >= sizeof(pair)) {
            trytr_math_expression_destroy(compiled);
            return TRYTR_PLOT_FORMULA_LIMIT_EXCEEDED;
        }
        const size_t pair_length = (size_t)written;
        if (pair_length > TRYTR_DEFAULT_PLOT_BYTE_LIMIT - length) {
            trytr_math_expression_destroy(compiled);
            return TRYTR_PLOT_FORMULA_LIMIT_EXCEEDED;
        }
        memcpy(points + length, pair, pair_length);
        length += pair_length;
        if (index == 0u) {
            out_analysis->min_x = out_analysis->max_x = x;
            out_analysis->min_y = out_analysis->max_y = y;
        } else {
            if (x < out_analysis->min_x) out_analysis->min_x = x;
            if (x > out_analysis->max_x) out_analysis->max_x = x;
            if (y < out_analysis->min_y) out_analysis->min_y = y;
            if (y > out_analysis->max_y) out_analysis->max_y = y;
        }
        ++out_analysis->point_count;
    }
    points[length] = '\0';
    trytr_math_expression_destroy(compiled);
    *out_points = points;
    *out_length = length;
    return TRYTR_PLOT_FORMULA_OK;
}
