#include "trytr/math/curve.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define TRYTR_MATH_CUBIC_MAX_DEPTH 12u

static double distance_squared(TrytrMathPoint a, TrytrMathPoint b)
{
    /* Flattening validates coordinates to half the int range first. */
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
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

static TrytrMathPoint midpoint(TrytrMathPoint a, TrytrMathPoint b)
{
    return (TrytrMathPoint){a.x * 0.5 + b.x * 0.5,
                            a.y * 0.5 + b.y * 0.5};
}

static double point_segment_distance_squared(TrytrMathPoint point,
                                             TrytrMathPoint start,
                                             TrytrMathPoint end)
{
    const double chord_x = end.x - start.x;
    const double chord_y = end.y - start.y;
    const double chord_squared = chord_x * chord_x + chord_y * chord_y;
    if (chord_squared == 0.0) {
        return distance_squared(point, start);
    }

    const double offset_x = point.x - start.x;
    const double offset_y = point.y - start.y;
    const double projection =
        (offset_x * chord_x + offset_y * chord_y) / chord_squared;
    if (projection <= 0.0) return distance_squared(point, start);
    if (projection >= 1.0) return distance_squared(point, end);

    const double cross = offset_x * chord_y - offset_y * chord_x;
    return (cross * cross) / chord_squared;
}

static bool cubic_is_flat(const TrytrMathCubic *curve, double tolerance)
{
    const double tolerance_squared = tolerance * tolerance;
    /* A cubic lies in the convex hull of its controls. If every control is
     * within tolerance of the endpoint segment, the whole curve is too. */
    return point_segment_distance_squared(curve->control1,
                                          curve->start, curve->end) <=
               tolerance_squared &&
           point_segment_distance_squared(curve->control2,
                                          curve->start, curve->end) <=
               tolerance_squared;
}

static void split_cubic(const TrytrMathCubic *curve, TrytrMathCubic *left,
                        TrytrMathCubic *right)
{
    const TrytrMathPoint p01 = midpoint(curve->start, curve->control1);
    const TrytrMathPoint p12 = midpoint(curve->control1, curve->control2);
    const TrytrMathPoint p23 = midpoint(curve->control2, curve->end);
    const TrytrMathPoint p012 = midpoint(p01, p12);
    const TrytrMathPoint p123 = midpoint(p12, p23);
    const TrytrMathPoint center = midpoint(p012, p123);
    *left = (TrytrMathCubic){curve->start, p01, p012, center};
    *right = (TrytrMathCubic){center, p123, p23, curve->end};
}

static bool cubic_fits_depth(const TrytrMathCubic *curve, double tolerance,
                             unsigned int depth)
{
    if (cubic_is_flat(curve, tolerance)) return true;
    if (depth >= TRYTR_MATH_CUBIC_MAX_DEPTH) return false;

    TrytrMathCubic left;
    TrytrMathCubic right;
    split_cubic(curve, &left, &right);
    return cubic_fits_depth(&left, tolerance, depth + 1u) &&
           cubic_fits_depth(&right, tolerance, depth + 1u);
}

static void flatten_cubic_emit(const TrytrMathCubic *curve, double tolerance,
                               TrytrMathSegmentCallback callback,
                               void *user_data)
{
    if (cubic_is_flat(curve, tolerance)) {
        callback(curve->start, curve->end, user_data);
        return;
    }

    TrytrMathCubic left;
    TrytrMathCubic right;
    split_cubic(curve, &left, &right);
    flatten_cubic_emit(&left, tolerance, callback, user_data);
    flatten_cubic_emit(&right, tolerance, callback, user_data);
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
    if (!cubic_fits_depth(curve, tolerance, 0u)) return false;
    flatten_cubic_emit(curve, tolerance, callback, user_data);
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
