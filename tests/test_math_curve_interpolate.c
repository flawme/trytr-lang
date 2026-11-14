#include "test.h"

#include "trytr/math/curve.h"

#include <float.h>
#include <math.h>

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

static bool test_curve_evaluation_stays_inside_extreme_control_hull(void)
{
    const TrytrMathCubic curve = {
        .start = {DBL_MAX, -DBL_MAX},
        .control1 = {DBL_MAX, -DBL_MAX},
        .control2 = {DBL_MAX, -DBL_MAX},
        .end = {DBL_MAX, -DBL_MAX}
    };
    const TrytrMathPoint sample = trytr_math_cubic_evaluate(&curve, 0.37);

    TEST_ASSERT(isfinite(sample.x));
    TEST_ASSERT(isfinite(sample.y));
    TEST_ASSERT(sample.x == DBL_MAX);
    TEST_ASSERT(sample.y == -DBL_MAX);
    TEST_ASSERT(trytr_math_cubic_evaluate(&curve, -1.0).x == DBL_MAX);
    TEST_ASSERT(trytr_math_cubic_evaluate(&curve, 2.0).x == DBL_MAX);
    return true;
}

static bool test_nonmonotone_curve_build_is_scale_stable(void)
{
    static const TrytrMathPoint unit_points[] = {
        {0.0, 0.0}, {1.0, 1.0}, {0.5, 2.0}, {1.5, 1.0}
    };
    static const TrytrMathPoint points[] = {
        {0.0, 0.0}, {1e200, 1e200}, {0.5e200, 2e200}, {1.5e200, 1e200}
    };
    static const TrytrMathPoint tiny_points[] = {
        {0.0, 0.0}, {1e-200, 1e-200}, {0.5e-200, 2e-200},
        {1.5e-200, 1e-200}
    };
    TrytrMathCubic unit_segments[3];
    TrytrMathCubic tiny_segments[3];
    TrytrMathCubic segments[3];
    TEST_ASSERT(trytr_math_build_curve(unit_points, 4u, unit_segments));
    TEST_ASSERT(trytr_math_build_curve(points, 4u, segments));
    TEST_ASSERT(trytr_math_build_curve(tiny_points, 4u, tiny_segments));

    for (size_t index = 0u; index < 3u; ++index) {
        TEST_ASSERT(isfinite(segments[index].control1.x));
        TEST_ASSERT(isfinite(segments[index].control1.y));
        TEST_ASSERT(isfinite(segments[index].control2.x));
        TEST_ASSERT(isfinite(segments[index].control2.y));
        TEST_ASSERT(segments[index].start.x == points[index].x);
        TEST_ASSERT(segments[index].start.y == points[index].y);
        TEST_ASSERT(segments[index].end.x == points[index + 1u].x);
        TEST_ASSERT(segments[index].end.y == points[index + 1u].y);
        TEST_ASSERT(close_to(segments[index].control1.x / 1e200,
                             unit_segments[index].control1.x));
        TEST_ASSERT(close_to(segments[index].control1.y / 1e200,
                             unit_segments[index].control1.y));
        TEST_ASSERT(close_to(segments[index].control2.x / 1e200,
                             unit_segments[index].control2.x));
        TEST_ASSERT(close_to(segments[index].control2.y / 1e200,
                             unit_segments[index].control2.y));
        TEST_ASSERT(close_to(tiny_segments[index].control1.x / 1e-200,
                             unit_segments[index].control1.x));
        TEST_ASSERT(close_to(tiny_segments[index].control1.y / 1e-200,
                             unit_segments[index].control1.y));
        TEST_ASSERT(close_to(tiny_segments[index].control2.x / 1e-200,
                             unit_segments[index].control2.x));
        TEST_ASSERT(close_to(tiny_segments[index].control2.y / 1e-200,
                             unit_segments[index].control2.y));
    }
    return true;
}

static bool test_two_point_curve_survives_full_finite_range(void)
{
    const TrytrMathPoint points[] = {
        {-DBL_MAX, -DBL_MAX}, {DBL_MAX, DBL_MAX}
    };
    TrytrMathCubic segment;

    TEST_ASSERT(trytr_math_build_curve(points, 2u, &segment));
    TEST_ASSERT(isfinite(segment.control1.x));
    TEST_ASSERT(isfinite(segment.control1.y));
    TEST_ASSERT(isfinite(segment.control2.x));
    TEST_ASSERT(isfinite(segment.control2.y));
    const TrytrMathPoint middle = trytr_math_cubic_evaluate(&segment, 0.5);
    TEST_ASSERT(isfinite(middle.x) && isfinite(middle.y));
    TEST_ASSERT(fabs(middle.x / DBL_MAX) < 1e-15);
    TEST_ASSERT(fabs(middle.y / DBL_MAX) < 1e-15);
    return true;
}

static bool test_extreme_finite_data_never_reports_nonfinite_controls(void)
{
    const TrytrMathPoint monotone_x_points[] = {
        {0.0, DBL_MAX}, {1.0, -DBL_MAX}, {2.0, DBL_MAX}
    };
    const TrytrMathPoint looping_points[] = {
        {0.0, DBL_MAX}, {2.0, -DBL_MAX}, {1.0, DBL_MAX}, {3.0, -DBL_MAX}
    };
    const TrytrMathPoint *point_sets[] = {monotone_x_points, looping_points};
    const size_t counts[] = {3u, 4u};

    for (size_t set = 0u; set < 2u; ++set) {
        TrytrMathCubic segments[3];
        if (!trytr_math_build_curve(point_sets[set], counts[set], segments))
            continue;
        for (size_t index = 0u; index + 1u < counts[set]; ++index) {
            TEST_ASSERT(isfinite(segments[index].start.x));
            TEST_ASSERT(isfinite(segments[index].start.y));
            TEST_ASSERT(isfinite(segments[index].control1.x));
            TEST_ASSERT(isfinite(segments[index].control1.y));
            TEST_ASSERT(isfinite(segments[index].control2.x));
            TEST_ASSERT(isfinite(segments[index].control2.y));
            TEST_ASSERT(isfinite(segments[index].end.x));
            TEST_ASSERT(isfinite(segments[index].end.y));
        }
    }
    return true;
}

static const TrytrTestCase tests[] = {
    {"PCHIP shape and tangent continuity",
     test_pchip_preserves_monotone_data_and_shared_tangents},
    {"two point straight cubic", test_two_points_make_a_straight_cubic},
    {"exact quadratic conversion", test_quadratic_conversion_is_exact},
    {"descending monotone x", test_descending_x_uses_monotone_interpolation},
    {"centripetal path fallback", test_nonmonotone_paths_keep_each_data_point},
    {"cubic evaluation convex hull at extreme scale",
     test_curve_evaluation_stays_inside_extreme_control_hull},
    {"centripetal interpolation at extreme scale",
     test_nonmonotone_curve_build_is_scale_stable},
    {"two point interpolation across finite range",
     test_two_point_curve_survives_full_finite_range},
    {"finite curve data never yields nonfinite controls",
     test_extreme_finite_data_never_reports_nonfinite_controls}
};

TEST_MAIN(tests)
