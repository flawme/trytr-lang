#include "math_1.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static double distance_squared(TrytrMathPoint a, TrytrMathPoint b)
{
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
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

static double secant_slope(TrytrMathPoint a, TrytrMathPoint b)
{
    return (b.y - a.y) / (b.x - a.x);
}

static bool same_sign(double a, double b)
{
    return (a > 0.0 && b > 0.0) || (a < 0.0 && b < 0.0);
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
    return pow(distance_squared(a, b), 0.25);
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
    if (distance_squared(start, end) == 0.0) {
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

    if (!x_is_strictly_monotone(points, point_count)) {
        for (size_t index = 0u; index + 1u < point_count; ++index) {
            out_segments[index] = centripetal_segment(points, point_count, index);
        }
        return true;
    }

    for (size_t index = 0u; index + 1u < point_count; ++index) {
        const TrytrMathPoint start = points[index];
        const TrytrMathPoint end = points[index + 1u];
        const double dx = end.x - start.x;
        const double start_tangent = pchip_tangent(points, point_count, index);
        const double end_tangent = pchip_tangent(points, point_count, index + 1u);
        out_segments[index] = (TrytrMathCubic){
            .start = start,
            .control1 = {start.x + dx / 3.0,
                         start.y + start_tangent * dx / 3.0},
            .control2 = {end.x - dx / 3.0,
                         end.y - end_tangent * dx / 3.0},
            .end = end
        };
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
    const double inverse = 1.0 - parameter;
    const double inverse2 = inverse * inverse;
    const double parameter2 = parameter * parameter;
    return add(add(scale(curve->start, inverse2 * inverse),
                   scale(curve->control1, 3.0 * inverse2 * parameter)),
               add(scale(curve->control2, 3.0 * inverse * parameter2),
                   scale(curve->end, parameter2 * parameter)));
}

static double fractional_part(double value)
{
    return value - floor(value);
}

static void plot_pixel(bool steep, int x, int y, double coverage,
                       TrytrMathCoverageCallback callback, void *user_data)
{
    if (coverage <= 0.0) return;
    if (steep) callback(y, x, coverage, user_data);
    else callback(x, y, coverage, user_data);
}

static void rasterize_line(TrytrMathPoint start, TrytrMathPoint end,
                           TrytrMathCoverageCallback callback, void *user_data)
{
    bool steep = fabs(end.y - start.y) > fabs(end.x - start.x);
    if (steep) {
        const double temporary = start.x;
        start.x = start.y;
        start.y = temporary;
        const double end_temporary = end.x;
        end.x = end.y;
        end.y = end_temporary;
    }
    if (start.x > end.x) {
        const TrytrMathPoint temporary = start;
        start = end;
        end = temporary;
    }

    const double dx = end.x - start.x;
    if (dx == 0.0) {
        plot_pixel(steep, (int)lround(start.x), (int)lround(start.y), 1.0,
                   callback, user_data);
        return;
    }
    const double gradient = (end.y - start.y) / dx;
    const double first_x = floor(start.x + 0.5);
    const double first_y = start.y + gradient * (first_x - start.x);
    const double first_gap = 1.0 - fractional_part(start.x + 0.5);
    const int first_pixel_x = (int)first_x;
    const int first_pixel_y = (int)floor(first_y);
    plot_pixel(steep, first_pixel_x, first_pixel_y,
               (1.0 - fractional_part(first_y)) * first_gap,
               callback, user_data);
    plot_pixel(steep, first_pixel_x, first_pixel_y + 1,
               fractional_part(first_y) * first_gap, callback, user_data);

    double intery = first_y + gradient;
    const double last_x = floor(end.x + 0.5);
    const double last_y = end.y + gradient * (last_x - end.x);
    const double last_gap = fractional_part(end.x + 0.5);
    const int last_pixel_x = (int)last_x;
    const int last_pixel_y = (int)floor(last_y);
    for (int x = first_pixel_x + 1; x < last_pixel_x; ++x) {
        const int y = (int)floor(intery);
        plot_pixel(steep, x, y, 1.0 - fractional_part(intery),
                   callback, user_data);
        plot_pixel(steep, x, y + 1, fractional_part(intery),
                   callback, user_data);
        intery += gradient;
    }
    if (last_pixel_x != first_pixel_x) {
        plot_pixel(steep, last_pixel_x, last_pixel_y,
                   (1.0 - fractional_part(last_y)) * last_gap,
                   callback, user_data);
        plot_pixel(steep, last_pixel_x, last_pixel_y + 1,
                   fractional_part(last_y) * last_gap, callback, user_data);
    }
}

bool trytr_math_rasterize_line(TrytrMathPoint start, TrytrMathPoint end,
                               TrytrMathCoverageCallback callback,
                               void *user_data)
{
    if (callback == NULL || !isfinite(start.x) || !isfinite(start.y) ||
        !isfinite(end.x) || !isfinite(end.y) ||
        fabs(start.x) > (double)(INT32_MAX / 2) ||
        fabs(start.y) > (double)(INT32_MAX / 2) ||
        fabs(end.x) > (double)(INT32_MAX / 2) ||
        fabs(end.y) > (double)(INT32_MAX / 2)) return false;
    rasterize_line(start, end, callback, user_data);
    return true;
}

bool trytr_math_trace_line(TrytrMathPoint start, TrytrMathPoint end,
                           TrytrMathCellCallback callback,
                           void *user_data)
{
    if (callback == NULL || !isfinite(start.x) || !isfinite(start.y) ||
        !isfinite(end.x) || !isfinite(end.y) ||
        fabs(start.x) > (double)(INT32_MAX / 2) ||
        fabs(start.y) > (double)(INT32_MAX / 2) ||
        fabs(end.x) > (double)(INT32_MAX / 2) ||
        fabs(end.y) > (double)(INT32_MAX / 2)) return false;

    int64_t x = (int64_t)lround(start.x);
    int64_t y = (int64_t)lround(start.y);
    const int64_t end_x = (int64_t)lround(end.x);
    const int64_t end_y = (int64_t)lround(end.y);
    const int64_t delta_x = end_x - x;
    const int64_t delta_y = end_y - y;
    const int64_t dx = delta_x < 0 ? -delta_x : delta_x;
    const int64_t sx = x < end_x ? 1 : -1;
    const int64_t dy = -(delta_y < 0 ? -delta_y : delta_y);
    const int64_t sy = y < end_y ? 1 : -1;
    int64_t error = dx + dy;

    for (;;) {
        if (x == end_x && y == end_y) break;
        const int64_t doubled_error = 2 * error;
        const int64_t previous_x = x;
        const int64_t previous_y = y;
        bool stepped_x = false;
        bool stepped_y = false;
        if (doubled_error >= dy) {
            error += dy;
            x += sx;
            stepped_x = true;
        }
        if (doubled_error <= dx) {
            error += dx;
            y += sy;
            stepped_y = true;
        }
        callback((int)previous_x, (int)previous_y, user_data);
        if (stepped_x && stepped_y)
            callback((int)x, (int)previous_y, user_data);
    }
    callback((int)end_x, (int)end_y, user_data);
    return true;
}

static bool cubic_is_flat(const TrytrMathCubic *curve, double tolerance)
{
    const double chord_x = curve->end.x - curve->start.x;
    const double chord_y = curve->end.y - curve->start.y;
    const double chord_squared = chord_x * chord_x + chord_y * chord_y;
    if (chord_squared == 0.0) {
        return distance_squared(curve->start, curve->control1) <= tolerance * tolerance &&
               distance_squared(curve->start, curve->control2) <= tolerance * tolerance;
    }

    const TrytrMathPoint controls[] = {curve->control1, curve->control2};
    const double tolerance_squared = tolerance * tolerance;
    double polygon_length = 0.0;
    TrytrMathPoint previous = curve->start;
    for (size_t index = 0u; index < 2u; ++index) {
        const TrytrMathPoint point = controls[index];
        const double vx = point.x - curve->start.x;
        const double vy = point.y - curve->start.y;
        const double projection = (vx * chord_x + vy * chord_y) / chord_squared;
        const double cross = vx * chord_y - vy * chord_x;
        if ((cross * cross) / chord_squared > tolerance_squared ||
            projection < -0.05 || projection > 1.05) return false;
        polygon_length += sqrt(distance_squared(previous, point));
        previous = point;
    }
    polygon_length += sqrt(distance_squared(previous, curve->end));
    return polygon_length - sqrt(chord_squared) <= tolerance * 2.0;
}

static TrytrMathPoint midpoint(TrytrMathPoint a, TrytrMathPoint b)
{
    return scale(add(a, b), 0.5);
}

static void flatten_cubic_recursive(const TrytrMathCubic *curve,
                                    double tolerance, unsigned int depth,
                                    TrytrMathSegmentCallback callback,
                                    void *user_data)
{
    if (depth >= 12u || cubic_is_flat(curve, tolerance)) {
        callback(curve->start, curve->end, user_data);
        return;
    }

    const TrytrMathPoint p01 = midpoint(curve->start, curve->control1);
    const TrytrMathPoint p12 = midpoint(curve->control1, curve->control2);
    const TrytrMathPoint p23 = midpoint(curve->control2, curve->end);
    const TrytrMathPoint p012 = midpoint(p01, p12);
    const TrytrMathPoint p123 = midpoint(p12, p23);
    const TrytrMathPoint center = midpoint(p012, p123);
    const TrytrMathCubic left = {curve->start, p01, p012, center};
    const TrytrMathCubic right = {center, p123, p23, curve->end};
    flatten_cubic_recursive(&left, tolerance, depth + 1u, callback, user_data);
    flatten_cubic_recursive(&right, tolerance, depth + 1u, callback, user_data);
}

static bool cubic_arguments_valid(const TrytrMathCubic *curve, double tolerance)
{
    if (curve == NULL || !isfinite(tolerance) || tolerance <= 0.0) return false;
    const TrytrMathPoint points[] = {
        curve->start, curve->control1, curve->control2, curve->end
    };
    for (size_t index = 0u; index < sizeof(points) / sizeof(points[0]); ++index) {
        if (!isfinite(points[index].x) || !isfinite(points[index].y) ||
            fabs(points[index].x) > (double)(INT32_MAX / 2) ||
            fabs(points[index].y) > (double)(INT32_MAX / 2)) {
            return false;
        }
    }
    return true;
}

bool trytr_math_flatten_cubic(const TrytrMathCubic *curve,
                              double tolerance,
                              TrytrMathSegmentCallback callback,
                              void *user_data)
{
    if (!cubic_arguments_valid(curve, tolerance) || callback == NULL) return false;
    flatten_cubic_recursive(curve, tolerance, 0u, callback, user_data);
    return true;
}

typedef struct RasterizeCubicContext {
    TrytrMathCoverageCallback callback;
    void *user_data;
} RasterizeCubicContext;

static void rasterize_flattened_segment(TrytrMathPoint start,
                                        TrytrMathPoint end,
                                        void *user_data)
{
    RasterizeCubicContext *context = user_data;
    rasterize_line(start, end, context->callback, context->user_data);
}

bool trytr_math_rasterize_cubic(const TrytrMathCubic *curve,
                                double tolerance,
                                TrytrMathCoverageCallback callback,
                                void *user_data)
{
    if (callback == NULL) return false;
    RasterizeCubicContext context = {.callback = callback, .user_data = user_data};
    return trytr_math_flatten_cubic(curve, tolerance,
                                    rasterize_flattened_segment, &context);
}
