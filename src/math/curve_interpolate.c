#include "trytr/math/curve.h"

#include <math.h>

static double distance(TrytrMathPoint a, TrytrMathPoint b)
{
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return hypot(dx, dy);
}

static TrytrMathPoint add(TrytrMathPoint a, TrytrMathPoint b)
{
    return (TrytrMathPoint){a.x + b.x, a.y + b.y};
}

static TrytrMathPoint subtract(TrytrMathPoint a, TrytrMathPoint b)
{
    return (TrytrMathPoint){a.x - b.x, a.y - b.y};
}

static TrytrMathPoint scale(TrytrMathPoint point, double factor)
{
    return (TrytrMathPoint){point.x * factor, point.y * factor};
}

static double interpolate(double start, double end, double parameter)
{
    if (signbit(start) != signbit(end)) {
        return start * (1.0 - parameter) + end * parameter;
    }
    return fma(parameter, end - start, start);
}

static double secant_slope(TrytrMathPoint a, TrytrMathPoint b)
{
    return (b.y - a.y) / (b.x - a.x);
}

static bool same_sign(double a, double b)
{
    return (a > 0.0 && b > 0.0) || (a < 0.0 && b < 0.0);
}

static bool cubic_is_finite(const TrytrMathCubic *curve)
{
    return isfinite(curve->control1.x) && isfinite(curve->control1.y) &&
           isfinite(curve->control2.x) && isfinite(curve->control2.y);
}

static double pchip_endpoint_tangent(double h0, double h1,
                                     double slope0, double slope1)
{
    double tangent = ((2.0 * h0 + h1) * slope0 - h0 * slope1) / (h0 + h1);
    if (!same_sign(tangent, slope0)) return 0.0;
    if (!same_sign(slope0, slope1) && fabs(tangent) > 3.0 * fabs(slope0)) {
        return 3.0 * slope0;
    }
    return tangent;
}

static double pchip_tangent(const TrytrMathPoint *points, size_t count,
                            size_t index)
{
    if (count == 2u) return secant_slope(points[0], points[1]);
    if (index == 0u) {
        const double h0 = fabs(points[1].x - points[0].x);
        const double h1 = fabs(points[2].x - points[1].x);
        return pchip_endpoint_tangent(h0, h1,
                                      secant_slope(points[0], points[1]),
                                      secant_slope(points[1], points[2]));
    }
    if (index + 1u == count) {
        const double h0 = fabs(points[count - 1u].x - points[count - 2u].x);
        const double h1 = fabs(points[count - 2u].x - points[count - 3u].x);
        return pchip_endpoint_tangent(h0, h1,
                                      secant_slope(points[count - 2u],
                                                   points[count - 1u]),
                                      secant_slope(points[count - 3u],
                                                   points[count - 2u]));
    }

    const double previous_h = fabs(points[index].x - points[index - 1u].x);
    const double next_h = fabs(points[index + 1u].x - points[index].x);
    const double previous_slope = secant_slope(points[index - 1u], points[index]);
    const double next_slope = secant_slope(points[index], points[index + 1u]);
    if (!same_sign(previous_slope, next_slope)) return 0.0;

    const double w1 = 2.0 * next_h + previous_h;
    const double w2 = next_h + 2.0 * previous_h;
    return (w1 + w2) /
           (w1 / previous_slope + w2 / next_slope);
}

static bool x_is_strictly_monotone(const TrytrMathPoint *points, size_t count)
{
    const double first_delta = points[1].x - points[0].x;
    if (first_delta == 0.0) return false;
    const bool increasing = first_delta > 0.0;
    for (size_t index = 1u; index + 1u < count; ++index) {
        const double delta = points[index + 1u].x - points[index].x;
        if ((increasing && delta <= 0.0) || (!increasing && delta >= 0.0)) return false;
    }
    return true;
}

static TrytrMathPoint extrapolate(TrytrMathPoint fixed, TrytrMathPoint other)
{
    return subtract(scale(fixed, 2.0), other);
}

static double centripetal_interval(TrytrMathPoint a, TrytrMathPoint b)
{
    return sqrt(distance(a, b));
}

static TrytrMathPoint centripetal_tangent(TrytrMathPoint previous,
                                         TrytrMathPoint current,
                                         TrytrMathPoint next,
                                         double previous_time,
                                         double current_time,
                                         double next_time,
                                         double interval)
{
    const TrytrMathPoint before = scale(subtract(current, previous),
                                        1.0 / (current_time - previous_time));
    const TrytrMathPoint across = scale(subtract(next, previous),
                                        1.0 / (next_time - previous_time));
    const TrytrMathPoint after = scale(subtract(next, current),
                                       1.0 / (next_time - current_time));
    return scale(add(subtract(before, across), after), interval);
}

static TrytrMathCubic centripetal_segment(const TrytrMathPoint *points,
                                         size_t count, size_t index)
{
    const TrytrMathPoint start = points[index];
    const TrytrMathPoint end = points[index + 1u];
    if (start.x == end.x && start.y == end.y) {
        return (TrytrMathCubic){start, start, end, end};
    }

    const TrytrMathPoint previous = index == 0u
        ? extrapolate(start, end) : points[index - 1u];
    const TrytrMathPoint next = index + 2u >= count
        ? extrapolate(end, start) : points[index + 2u];
    const double t0 = 0.0;
    const double t1 = t0 + centripetal_interval(previous, start);
    const double t2 = t1 + centripetal_interval(start, end);
    const double t3 = t2 + centripetal_interval(end, next);
    if (t1 == t0 || t2 == t1 || t3 == t2) {
        const TrytrMathPoint delta = scale(subtract(end, start), 1.0 / 3.0);
        return (TrytrMathCubic){start, add(start, delta), subtract(end, delta), end};
    }

    const double interval = t2 - t1;
    const TrytrMathPoint tangent1 = centripetal_tangent(previous, start, end,
                                                       t0, t1, t2, interval);
    const TrytrMathPoint tangent2 = centripetal_tangent(start, end, next,
                                                       t1, t2, t3, interval);
    return (TrytrMathCubic){
        .start = start,
        .control1 = add(start, scale(tangent1, 1.0 / 3.0)),
        .control2 = subtract(end, scale(tangent2, 1.0 / 3.0)),
        .end = end
    };
}

bool trytr_math_build_curve(const TrytrMathPoint *points, size_t point_count,
                            TrytrMathCubic *out_segments)
{
    if (points == NULL || out_segments == NULL || point_count < 2u) return false;
    for (size_t index = 0u; index < point_count; ++index) {
        if (!isfinite(points[index].x) || !isfinite(points[index].y)) return false;
    }

    if (point_count == 2u) {
        out_segments[0] = (TrytrMathCubic){
            .start = points[0],
            .control1 = {
                interpolate(points[0].x, points[1].x, 1.0 / 3.0),
                interpolate(points[0].y, points[1].y, 1.0 / 3.0)
            },
            .control2 = {
                interpolate(points[0].x, points[1].x, 2.0 / 3.0),
                interpolate(points[0].y, points[1].y, 2.0 / 3.0)
            },
            .end = points[1]
        };
        return cubic_is_finite(&out_segments[0]);
    }

    if (!x_is_strictly_monotone(points, point_count)) {
        for (size_t index = 0u; index + 1u < point_count; ++index) {
            const TrytrMathCubic segment =
                centripetal_segment(points, point_count, index);
            if (!cubic_is_finite(&segment)) return false;
            out_segments[index] = segment;
        }
        return true;
    }

    double start_tangent = pchip_tangent(points, point_count, 0u);
    for (size_t index = 0u; index + 1u < point_count; ++index) {
        const TrytrMathPoint start = points[index];
        const TrytrMathPoint end = points[index + 1u];
        const double dx = end.x - start.x;
        const double end_tangent = pchip_tangent(points, point_count, index + 1u);
        const TrytrMathCubic segment = {
            .start = start,
            .control1 = {start.x + dx / 3.0,
                         start.y + start_tangent * dx / 3.0},
            .control2 = {end.x - dx / 3.0,
                         end.y - end_tangent * dx / 3.0},
            .end = end
        };
        if (!cubic_is_finite(&segment)) return false;
        out_segments[index] = segment;
        start_tangent = end_tangent;
    }
    return true;
}

TrytrMathCubic trytr_math_quadratic_to_cubic(TrytrMathPoint start,
                                             TrytrMathPoint control,
                                             TrytrMathPoint end)
{
    const TrytrMathPoint first_control = add(
        start, scale(subtract(control, start), 2.0 / 3.0));
    const TrytrMathPoint second_control = add(
        end, scale(subtract(control, end), 2.0 / 3.0));
    return (TrytrMathCubic){start, first_control, second_control, end};
}

TrytrMathPoint trytr_math_cubic_evaluate(const TrytrMathCubic *curve,
                                         double parameter)
{
    if (curve == NULL) return (TrytrMathPoint){0.0, 0.0};
    if (parameter < 0.0) parameter = 0.0;
    if (parameter > 1.0) parameter = 1.0;
    const TrytrMathPoint first = {
        interpolate(curve->start.x, curve->control1.x, parameter),
        interpolate(curve->start.y, curve->control1.y, parameter)
    };
    const TrytrMathPoint second = {
        interpolate(curve->control1.x, curve->control2.x, parameter),
        interpolate(curve->control1.y, curve->control2.y, parameter)
    };
    const TrytrMathPoint third = {
        interpolate(curve->control2.x, curve->end.x, parameter),
        interpolate(curve->control2.y, curve->end.y, parameter)
    };
    const TrytrMathPoint left = {
        interpolate(first.x, second.x, parameter),
        interpolate(first.y, second.y, parameter)
    };
    const TrytrMathPoint right = {
        interpolate(second.x, third.x, parameter),
        interpolate(second.y, third.y, parameter)
    };
    return (TrytrMathPoint){
        interpolate(left.x, right.x, parameter),
        interpolate(left.y, right.y, parameter)
    };
}
