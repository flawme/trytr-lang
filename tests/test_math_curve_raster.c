#include "test.h"

#include "trytr/math/curve.h"

#include <math.h>
#include <stdlib.h>

static bool close_to(double actual, double expected)
{
    return fabs(actual - expected) < 1e-8;
}

typedef struct RasterCapture {
    size_t samples;
    bool saw_high_curve_point;
    double maximum_coverage;
    bool coordinates_valid;
} RasterCapture;

static void capture_coverage(int x, int y, double coverage, void *user_data)
{
    RasterCapture *capture = user_data;
    ++capture->samples;
    if (y > 5) capture->saw_high_curve_point = true;
    if (coverage > capture->maximum_coverage) capture->maximum_coverage = coverage;
    if (x < -1 || x > 20 || y < -1 || y > 20 || coverage < 0.0 || coverage > 1.0) {
        capture->coordinates_valid = false;
    }
}

static bool test_adaptive_rasterizer_samples_curve_with_coverage(void)
{
    const TrytrMathCubic curve = {
        .start = {0.0, 0.0},
        .control1 = {0.0, 10.0},
        .control2 = {10.0, 10.0},
        .end = {10.0, 0.0}
    };
    RasterCapture capture = {.coordinates_valid = true};
    TEST_ASSERT(trytr_math_rasterize_cubic(&curve, 0.15,
                                           capture_coverage, &capture));
    TEST_ASSERT(capture.samples > 10u);
    TEST_ASSERT(capture.saw_high_curve_point);
    TEST_ASSERT(capture.maximum_coverage > 0.0 && capture.maximum_coverage <= 1.0);
    TEST_ASSERT(capture.coordinates_valid);
    return true;
}

typedef struct FlattenCapture {
    TrytrMathPoint starts[512];
    TrytrMathPoint ends[512];
    size_t segments;
    TrytrMathPoint previous_end;
    bool continuous;
    bool within_capacity;
} FlattenCapture;

static void capture_flat_segment(TrytrMathPoint start, TrytrMathPoint end,
                                 void *user_data)
{
    FlattenCapture *capture = user_data;
    if (capture->segments != 0u &&
        (!close_to(start.x, capture->previous_end.x) ||
         !close_to(start.y, capture->previous_end.y))) {
        capture->continuous = false;
    }
    if (capture->segments < sizeof(capture->starts) / sizeof(capture->starts[0])) {
        capture->starts[capture->segments] = start;
        capture->ends[capture->segments] = end;
    } else {
        capture->within_capacity = false;
    }
    capture->previous_end = end;
    ++capture->segments;
}

static bool test_adaptive_flattening_emits_connected_segments(void)
{
    const TrytrMathCubic curve = {
        .start = {0.0, 0.0},
        .control1 = {0.0, 10.0},
        .control2 = {10.0, 10.0},
        .end = {10.0, 0.0}
    };
    FlattenCapture capture = {.continuous = true, .within_capacity = true};
    TEST_ASSERT(trytr_math_flatten_cubic(&curve, 0.12,
                                         capture_flat_segment, &capture));
    TEST_ASSERT(capture.segments > 4u);
    TEST_ASSERT(capture.continuous);
    TEST_ASSERT(capture.within_capacity);
    TEST_ASSERT(close_to(capture.previous_end.x, curve.end.x));
    TEST_ASSERT(close_to(capture.previous_end.y, curve.end.y));
    return true;
}

static double point_segment_distance(TrytrMathPoint point,
                                     TrytrMathPoint start,
                                     TrytrMathPoint end)
{
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double length_squared = dx * dx + dy * dy;
    if (length_squared == 0.0) return hypot(point.x - start.x, point.y - start.y);

    const double projection = ((point.x - start.x) * dx +
                               (point.y - start.y) * dy) / length_squared;
    const double clamped = fmax(0.0, fmin(1.0, projection));
    const double closest_x = start.x + clamped * dx;
    const double closest_y = start.y + clamped * dy;
    return hypot(point.x - closest_x, point.y - closest_y);
}

static bool test_flattening_respects_screen_space_error(void)
{
    const TrytrMathCubic curve = {
        .start = {0.0, 0.0},
        .control1 = {40.0, 100.0},
        .control2 = {60.0, -80.0},
        .end = {100.0, 10.0}
    };
    const double tolerance = 0.35;
    FlattenCapture capture = {.continuous = true, .within_capacity = true};
    TEST_ASSERT(trytr_math_flatten_cubic(&curve, tolerance,
                                         capture_flat_segment, &capture));
    TEST_ASSERT(capture.segments > 1u);
    TEST_ASSERT(capture.within_capacity);

    for (int step = 0; step <= 2000; ++step) {
        const TrytrMathPoint sample = trytr_math_cubic_evaluate(
            &curve, (double)step / 2000.0);
        double nearest = INFINITY;
        for (size_t index = 0u; index < capture.segments; ++index) {
            nearest = fmin(nearest, point_segment_distance(
                sample, capture.starts[index], capture.ends[index]));
        }
        TEST_ASSERT(nearest <= tolerance + 1e-3);
    }
    return true;
}

static bool test_flattening_rejects_tolerance_beyond_its_budget(void)
{
    const TrytrMathCubic curve = {
        .start = {0.0, 0.0},
        .control1 = {0.0, 10.0},
        .control2 = {10.0, 10.0},
        .end = {10.0, 0.0}
    };
    FlattenCapture capture = {.continuous = true, .within_capacity = true};
    TEST_ASSERT(!trytr_math_flatten_cubic(&curve, 1e-12,
                                          capture_flat_segment, &capture));
    TEST_ASSERT_SIZE(0u, capture.segments);
    return true;
}

static bool test_line_rasterizer_emits_fractional_coverage(void)
{
    RasterCapture capture = {.coordinates_valid = true};
    TEST_ASSERT(trytr_math_rasterize_line((TrytrMathPoint){0.0, 0.0},
                                          (TrytrMathPoint){10.0, 4.5},
                                          capture_coverage, &capture));
    TEST_ASSERT(capture.samples > 10u);
    TEST_ASSERT(capture.maximum_coverage > 0.0 && capture.maximum_coverage <= 1.0);
    TEST_ASSERT(capture.coordinates_valid);
    return true;
}

typedef struct GridLineCapture {
    int x[64];
    int y[64];
    size_t cells;
    bool connected;
} GridLineCapture;

static void capture_grid_cell(int x, int y, void *user_data)
{
    GridLineCapture *capture = user_data;
    if (capture->cells > 0u) {
        const int dx = x - capture->x[capture->cells - 1u];
        const int dy = y - capture->y[capture->cells - 1u];
        if (abs(dx) + abs(dy) != 1)
            capture->connected = false;
    }
    if (capture->cells < sizeof(capture->x) / sizeof(capture->x[0])) {
        capture->x[capture->cells] = x;
        capture->y[capture->cells] = y;
        ++capture->cells;
    } else {
        capture->connected = false;
    }
}

static bool test_connected_grid_line_has_no_cell_gaps(void)
{
    GridLineCapture capture = {.connected = true};
    TEST_ASSERT(trytr_math_trace_line((TrytrMathPoint){0.0, 0.0},
                                      (TrytrMathPoint){12.0, 5.0},
                                      capture_grid_cell, &capture));
    TEST_ASSERT(capture.cells == 18u);
    TEST_ASSERT(capture.connected);
    TEST_ASSERT_INT(0, capture.x[0]);
    TEST_ASSERT_INT(0, capture.y[0]);
    TEST_ASSERT_INT(12, capture.x[capture.cells - 1u]);
    TEST_ASSERT_INT(5, capture.y[capture.cells - 1u]);
    TEST_ASSERT(!trytr_math_trace_line((TrytrMathPoint){NAN, 0.0},
                                       (TrytrMathPoint){1.0, 1.0},
                                       capture_grid_cell, &capture));
    TEST_ASSERT(!trytr_math_trace_line((TrytrMathPoint){0.0, 0.0},
                                       (TrytrMathPoint){1.0, 1.0}, NULL,
                                       &capture));
    return true;
}

static bool test_rasterizer_rejects_invalid_inputs(void)
{
    const TrytrMathCubic curve = {{0.0, 0.0}, {1.0, 1.0}, {2.0, 2.0}, {3.0, 3.0}};
    RasterCapture capture = {.coordinates_valid = true};
    TrytrMathCubic output;
    TEST_ASSERT(!trytr_math_build_curve(NULL, 2u, &output));
    const TrytrMathPoint invalid_points[] = {{0.0, 0.0}, {NAN, 1.0}};
    TEST_ASSERT(!trytr_math_build_curve(invalid_points, 2u, &output));
    TEST_ASSERT(!trytr_math_rasterize_line((TrytrMathPoint){0.0, 0.0},
                                           (TrytrMathPoint){1.0, 1.0},
                                           NULL, &capture));
    TEST_ASSERT(!trytr_math_rasterize_cubic(&curve, 0.0,
                                            capture_coverage, &capture));
    TEST_ASSERT(!trytr_math_rasterize_cubic(&curve, 0.2, NULL, &capture));
    TEST_ASSERT(!trytr_math_flatten_cubic(&curve, 0.0,
                                          capture_flat_segment, NULL));
    TEST_ASSERT(!trytr_math_flatten_cubic(&curve, 0.2, NULL, &capture));
    return true;
}

static const TrytrTestCase tests[] = {
    {"antialiased line rasterizer", test_line_rasterizer_emits_fractional_coverage},
    {"connected grid line rasterizer", test_connected_grid_line_has_no_cell_gaps},
    {"adaptive antialiased rasterizer",
     test_adaptive_rasterizer_samples_curve_with_coverage},
    {"adaptive connected curve flattening",
     test_adaptive_flattening_emits_connected_segments},
    {"curve flattening error bound", test_flattening_respects_screen_space_error},
    {"curve flattening work limit", test_flattening_rejects_tolerance_beyond_its_budget},
    {"invalid math inputs", test_rasterizer_rejects_invalid_inputs}
};

TEST_MAIN(tests)
