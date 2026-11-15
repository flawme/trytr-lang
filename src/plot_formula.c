#include "plot_formula.h"

#include "limits.h"
#include "trytr/math/expression.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define TRYTR_FORMULA_SAMPLE_LIMIT 1024u
#define TRYTR_FORMULA_RECURSION_LIMIT 12u
#define TRYTR_FORMULA_PIXEL_TOLERANCE 0.5
#define TRYTR_FORMULA_TEXT_LIMIT 64u

typedef struct FormulaPoint {
    double x;
    double y;
} FormulaPoint;

typedef struct FormulaSampler {
    const TrytrMathExpression *expression;
    FormulaPoint *points;
    size_t count;
    size_t capacity;
    size_t expression_length;
    double global_y_range;
    double screen_height;
    size_t *error_offset;
    TrytrPlotFormulaStatus status;
} FormulaSampler;

static bool evaluate_point(FormulaSampler *sampler, double x, FormulaPoint *out_point)
{
    out_point->x = x;
    if (!trytr_math_expression_evaluate(sampler->expression, x, &out_point->y) ||
        fabs(out_point->y) > (double)TRYTR_DEFAULT_COORDINATE_LIMIT) {
        *sampler->error_offset = sampler->expression_length;
        sampler->status = TRYTR_PLOT_FORMULA_DOMAIN;
        return false;
    }
    return true;
}

static bool append_point(FormulaSampler *sampler, FormulaPoint point)
{
    if (sampler->count >= sampler->capacity) {
        sampler->status = TRYTR_PLOT_FORMULA_RESOLUTION_LIMIT;
        return false;
    }
    sampler->points[sampler->count++] = point;
    return true;
}

static double interpolated_y(FormulaPoint start, FormulaPoint end, double fraction)
{
    return start.y * (1.0 - fraction) + end.y * fraction;
}

static bool refine_interval(FormulaSampler *sampler, FormulaPoint start,
                            FormulaPoint end, size_t depth)
{
    const double span = end.x - start.x;
    const double quarter_x = start.x + span * 0.25;
    const double midpoint_x = start.x + span * 0.5;
    const double three_quarter_x = start.x + span * 0.75;
    FormulaPoint quarter;
    FormulaPoint midpoint;
    FormulaPoint three_quarter;
    if (!evaluate_point(sampler, quarter_x, &quarter) ||
        !evaluate_point(sampler, midpoint_x, &midpoint) ||
        !evaluate_point(sampler, three_quarter_x, &three_quarter)) return false;

    double local_min = fmin(start.y, end.y);
    double local_max = fmax(start.y, end.y);
    local_min = fmin(local_min, fmin(quarter.y, fmin(midpoint.y, three_quarter.y)));
    local_max = fmax(local_max, fmax(quarter.y, fmax(midpoint.y, three_quarter.y)));
    const double scale_range = fmax(sampler->global_y_range, local_max - local_min);
    const double error = fmax(
        fabs(quarter.y - interpolated_y(start, end, 0.25)),
        fmax(fabs(midpoint.y - interpolated_y(start, end, 0.5)),
             fabs(three_quarter.y - interpolated_y(start, end, 0.75))));
    const double screen_error = scale_range == 0.0
        ? 0.0 : (error / scale_range) * sampler->screen_height;
    if (screen_error <= TRYTR_FORMULA_PIXEL_TOLERANCE) {
        return append_point(sampler, end);
    }
    if (depth >= TRYTR_FORMULA_RECURSION_LIMIT || midpoint_x == start.x ||
        midpoint_x == end.x) {
        sampler->status = TRYTR_PLOT_FORMULA_RESOLUTION_LIMIT;
        return false;
    }
    return refine_interval(sampler, start, midpoint, depth + 1u) &&
           refine_interval(sampler, midpoint, end, depth + 1u);
}

static void update_analysis(TrytrPlotAnalysis *analysis, FormulaPoint point)
{
    if (analysis->point_count == 0u) {
        analysis->min_x = analysis->max_x = point.x;
        analysis->min_y = analysis->max_y = point.y;
    } else {
        analysis->min_x = fmin(analysis->min_x, point.x);
        analysis->max_x = fmax(analysis->max_x, point.x);
        analysis->min_y = fmin(analysis->min_y, point.y);
        analysis->max_y = fmax(analysis->max_y, point.y);
    }
    ++analysis->point_count;
}

TrytrPlotFormulaStatus trytr_plot_formula_sample(
    const char *expression,
    size_t expression_length,
    double min_x,
    double max_x,
    size_t sample_count,
    double screen_height,
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
    if (!isfinite(min_x) || !isfinite(max_x) || !isfinite(screen_height) ||
        fabs(min_x) > (double)TRYTR_DEFAULT_COORDINATE_LIMIT ||
        fabs(max_x) > (double)TRYTR_DEFAULT_COORDINATE_LIMIT || min_x >= max_x ||
        screen_height <= 0.0 || sample_count < 2u) return TRYTR_PLOT_FORMULA_INVALID;

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

    const size_t point_capacity = TRYTR_DEFAULT_PLOT_POINT_LIMIT;
    FormulaPoint *samples = trytr_arena_alloc(arena, point_capacity * sizeof(*samples));
    FormulaPoint *baseline = trytr_arena_alloc(arena, sample_count * sizeof(*baseline));
    if (samples == NULL || baseline == NULL) {
        trytr_math_expression_destroy(compiled);
        return TRYTR_PLOT_FORMULA_NO_MEMORY;
    }
    FormulaSampler sampler = {
        .expression = compiled,
        .points = samples,
        .capacity = point_capacity,
        .expression_length = expression_length,
        .screen_height = screen_height,
        .error_offset = out_error_offset,
        .status = TRYTR_PLOT_FORMULA_OK,
    };
    double min_y = 0.0;
    double max_y = 0.0;
    for (size_t index = 0u; index < sample_count; ++index) {
        const double fraction = (double)index / (double)(sample_count - 1u);
        const double x = index + 1u == sample_count
            ? max_x : min_x + (max_x - min_x) * fraction;
        FormulaPoint sample;
        if (!evaluate_point(&sampler, x, &sample)) {
            trytr_math_expression_destroy(compiled);
            return TRYTR_PLOT_FORMULA_DOMAIN;
        }
        if (index == 0u) min_y = max_y = sample.y;
        else {
            min_y = fmin(min_y, sample.y);
            max_y = fmax(max_y, sample.y);
        }
        baseline[index] = sample;
    }
    sampler.global_y_range = max_y - min_y;
    if (!append_point(&sampler, baseline[0])) {
        trytr_math_expression_destroy(compiled);
        return TRYTR_PLOT_FORMULA_LIMIT_EXCEEDED;
    }
    for (size_t index = 0u; index + 1u < sample_count; ++index) {
        if (!refine_interval(&sampler, baseline[index], baseline[index + 1u], 0u)) {
            trytr_math_expression_destroy(compiled);
            return sampler.status == TRYTR_PLOT_FORMULA_OK
                ? TRYTR_PLOT_FORMULA_RESOLUTION_LIMIT : sampler.status;
        }
    }

    size_t capacity = sampler.count * TRYTR_FORMULA_TEXT_LIMIT;
    if (capacity > TRYTR_DEFAULT_PLOT_BYTE_LIMIT) {
        capacity = TRYTR_DEFAULT_PLOT_BYTE_LIMIT;
    }
    char *points = trytr_arena_alloc(arena, capacity + 1u);
    if (points == NULL) {
        trytr_math_expression_destroy(compiled);
        return TRYTR_PLOT_FORMULA_NO_MEMORY;
    }
    size_t length = 0u;
    for (size_t index = 0u; index < sampler.count; ++index) {
        char pair[TRYTR_FORMULA_TEXT_LIMIT];
        const int written = snprintf(pair, sizeof(pair), "%s%.17g,%.17g",
                                     index == 0u ? "" : " ",
                                     samples[index].x, samples[index].y);
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
        update_analysis(out_analysis, samples[index]);
    }
    points[length] = '\0';
    trytr_math_expression_destroy(compiled);
    *out_points = points;
    *out_length = length;
    return TRYTR_PLOT_FORMULA_OK;
}
