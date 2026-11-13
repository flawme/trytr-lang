#include "test.h"

#include "math_1.h"

#include <math.h>
#include <stdlib.h>

static bool close_to(double actual, double expected)
{
    return fabs(actual - expected) < 1e-8;
}

static bool test_pchip_preserves_monotone_data_and_shared_tangents(void)
{
    static const TrytrMathPoint points[] = {
        {0.0, 0.0}, {1.0, 1.0}, {3.0, 2.0}, {4.0, 6.0}
    };
    TrytrMathCubic segments[3];
    TEST_ASSERT(trytr_math_build_curve(points, 4u, segments));

    for (size_t index = 0u; index < 3u; ++index) {
        const double low = points[index].y;
        const double high = points[index + 1u].y;
        double previous = low;
        for (int step = 1; step <= 100; ++step) {
            const TrytrMathPoint sample = trytr_math_cubic_evaluate(
                &segments[index], (double)step / 100.0);
            TEST_ASSERT(sample.y >= low - 1e-8 && sample.y <= high + 1e-8);
            TEST_ASSERT(sample.y >= previous - 1e-8);
            previous = sample.y;
        }
    }

    for (size_t index = 0u; index + 1u < 3u; ++index) {
        const double dx_left = segments[index].end.x - segments[index].start.x;
        const double dx_right = segments[index + 1u].end.x - segments[index + 1u].start.x;
        const double tangent_left = 3.0 *
            (segments[index].end.y - segments[index].control2.y) / dx_left;
        const double tangent_right = 3.0 *
            (segments[index + 1u].control1.y - segments[index + 1u].start.y) / dx_right;
        TEST_ASSERT(close_to(tangent_left, tangent_right));
    }
    return true;
}

static bool test_two_points_make_a_straight_cubic(void)
{
    static const TrytrMathPoint points[] = {{2.0, 4.0}, {8.0, 10.0}};
    TrytrMathCubic segment;
    TEST_ASSERT(trytr_math_build_curve(points, 2u, &segment));
    TEST_ASSERT(close_to(segment.control1.x, 4.0));
    TEST_ASSERT(close_to(segment.control1.y, 6.0));
    TEST_ASSERT(close_to(segment.control2.x, 6.0));
    TEST_ASSERT(close_to(segment.control2.y, 8.0));
    const TrytrMathPoint middle = trytr_math_cubic_evaluate(&segment, 0.5);
    TEST_ASSERT(close_to(middle.x, 5.0));
    TEST_ASSERT(close_to(middle.y, 7.0));
    return true;
}

static bool test_quadratic_conversion_is_exact(void)
{
    const TrytrMathPoint start = {0.0, 0.0};
    const TrytrMathPoint control = {5.0, 10.0};
    const TrytrMathPoint end = {10.0, 0.0};
    const TrytrMathCubic cubic =
        trytr_math_quadratic_to_cubic(start, control, end);
    TEST_ASSERT(close_to(cubic.control1.x, 10.0 / 3.0));
    TEST_ASSERT(close_to(cubic.control1.y, 20.0 / 3.0));
    TEST_ASSERT(close_to(cubic.control2.x, 20.0 / 3.0));
    TEST_ASSERT(close_to(cubic.control2.y, 20.0 / 3.0));
    for (int step = 0; step <= 20; ++step) {
        const double t = (double)step / 20.0;
        const double inverse = 1.0 - t;
        const TrytrMathPoint quadratic = {
            inverse * inverse * start.x + 2.0 * inverse * t * control.x + t * t * end.x,
            inverse * inverse * start.y + 2.0 * inverse * t * control.y + t * t * end.y
        };
        const TrytrMathPoint cubic_point = trytr_math_cubic_evaluate(&cubic, t);
        TEST_ASSERT(close_to(quadratic.x, cubic_point.x));
        TEST_ASSERT(close_to(quadratic.y, cubic_point.y));
    }
    return true;
}

static bool test_descending_x_uses_monotone_interpolation(void)
{
    static const TrytrMathPoint points[] = {
        {4.0, 8.0}, {2.0, 5.0}, {0.0, 0.0}
    };
    TrytrMathCubic segments[2];
    TEST_ASSERT(trytr_math_build_curve(points, 3u, segments));
    for (size_t index = 0u; index < 2u; ++index) {
        double previous = points[index].y;
        for (int step = 1; step <= 100; ++step) {
            const TrytrMathPoint sample = trytr_math_cubic_evaluate(
                &segments[index], (double)step / 100.0);
            TEST_ASSERT(sample.y <= previous + 1e-8);
            TEST_ASSERT(sample.y >= points[index + 1u].y - 1e-8);
            previous = sample.y;
        }
    }
    return true;
}

static bool test_nonmonotone_paths_keep_each_data_point(void)
{
    static const TrytrMathPoint points[] = {
        {0.0, 0.0}, {2.0, 1.0}, {1.0, 2.0}, {3.0, 0.0}
    };
    TrytrMathCubic segments[3];
    TEST_ASSERT(trytr_math_build_curve(points, 4u, segments));
    for (size_t index = 0u; index < 3u; ++index) {
        TEST_ASSERT(close_to(segments[index].start.x, points[index].x));
        TEST_ASSERT(close_to(segments[index].start.y, points[index].y));
        TEST_ASSERT(close_to(segments[index].end.x, points[index + 1u].x));
        TEST_ASSERT(close_to(segments[index].end.y, points[index + 1u].y));
        TEST_ASSERT(isfinite(segments[index].control1.x));
        TEST_ASSERT(isfinite(segments[index].control1.y));
        TEST_ASSERT(isfinite(segments[index].control2.x));
        TEST_ASSERT(isfinite(segments[index].control2.y));
    }
    return true;
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
    size_t segments;
    TrytrMathPoint previous_end;
    bool continuous;
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
    FlattenCapture capture = {.continuous = true};
    TEST_ASSERT(trytr_math_flatten_cubic(&curve, 0.12,
                                         capture_flat_segment, &capture));
    TEST_ASSERT(capture.segments > 4u);
    TEST_ASSERT(capture.continuous);
    TEST_ASSERT(close_to(capture.previous_end.x, curve.end.x));
    TEST_ASSERT(close_to(capture.previous_end.y, curve.end.y));
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
    {"PCHIP shape and tangent continuity",
     test_pchip_preserves_monotone_data_and_shared_tangents},
    {"two point straight cubic", test_two_points_make_a_straight_cubic},
    {"exact quadratic conversion", test_quadratic_conversion_is_exact},
    {"descending monotone x", test_descending_x_uses_monotone_interpolation},
    {"centripetal path fallback", test_nonmonotone_paths_keep_each_data_point},
    {"antialiased line rasterizer", test_line_rasterizer_emits_fractional_coverage},
    {"connected grid line rasterizer", test_connected_grid_line_has_no_cell_gaps},
    {"adaptive antialiased rasterizer",
     test_adaptive_rasterizer_samples_curve_with_coverage},
    {"adaptive connected curve flattening",
     test_adaptive_flattening_emits_connected_segments},
    {"invalid math inputs", test_rasterizer_rejects_invalid_inputs}
};

TEST_MAIN(tests)
