#ifndef TRYTR_MATH_CURVE_H
#define TRYTR_MATH_CURVE_H

#include "trytr/math/point2.h"

#include <stdbool.h>
#include <stddef.h>

typedef TrytrPoint2 TrytrMathPoint;

typedef struct TrytrMathCubic {
    TrytrMathPoint start;
    TrytrMathPoint control1;
    TrytrMathPoint control2;
    TrytrMathPoint end;
} TrytrMathCubic;

typedef void (*TrytrMathCoverageCallback)(int x, int y, double coverage,
                                          void *user_data);
typedef void (*TrytrMathCellCallback)(int x, int y, void *user_data);
typedef void (*TrytrMathSegmentCallback)(TrytrMathPoint start,
                                         TrytrMathPoint end,
                                         void *user_data);

/* Build a C1 curve through every input point. Strictly monotone x data uses
 * shape-preserving PCHIP; other point paths use centripetal Catmull-Rom.
 * Returns false if the resulting control coordinates are not representable. */
bool trytr_math_build_curve(const TrytrMathPoint *points, size_t point_count,
                            TrytrMathCubic *out_segments);

TrytrMathCubic trytr_math_quadratic_to_cubic(TrytrMathPoint start,
                                             TrytrMathPoint control,
                                             TrytrMathPoint end);

TrytrMathPoint trytr_math_cubic_evaluate(const TrytrMathCubic *curve,
                                         double parameter);

/* Rasterize a straight segment with fractional pixel coverage. */
bool trytr_math_rasterize_line(TrytrMathPoint start, TrytrMathPoint end,
                               TrytrMathCoverageCallback callback,
                               void *user_data);

/* Trace a four-connected integer grid path approximating a line segment. */
bool trytr_math_trace_line(TrytrMathPoint start, TrytrMathPoint end,
                           TrytrMathCellCallback callback,
                           void *user_data);

/* Flatten curves to a screen-space tolerance and rasterize with Wu coverage.
 * Returns false if bounded subdivision cannot meet the tolerance; callbacks
 * are invoked only on success. */
bool trytr_math_rasterize_cubic(const TrytrMathCubic *curve,
                                double tolerance,
                                TrytrMathCoverageCallback callback,
                                void *user_data);

/* Flatten a cubic into connected line segments within screen-space tolerance.
 * Returns false if bounded subdivision cannot meet the tolerance; callbacks
 * are invoked only on success. */
bool trytr_math_flatten_cubic(const TrytrMathCubic *curve,
                              double tolerance,
                              TrytrMathSegmentCallback callback,
                              void *user_data);

#endif
