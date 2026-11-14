#ifndef TRYTR_MATH_VECTOR2_H
#define TRYTR_MATH_VECTOR2_H

#include "trytr/math/complex.h"

#include <stdbool.h>

typedef struct TrytrVector2 {
    TrytrReal x;
    TrytrReal y;
} TrytrVector2;

TrytrVector2 trytr_vector2_make(TrytrReal x, TrytrReal y);
TrytrVector2 trytr_vector2_add(TrytrVector2 left, TrytrVector2 right);
TrytrVector2 trytr_vector2_subtract(TrytrVector2 left, TrytrVector2 right);
TrytrVector2 trytr_vector2_scale(TrytrVector2 value, TrytrReal factor);
/* Products use a fast FMA path and an exponent-scaled fallback when direct
 * products overflow or underflow. The returned scalar can still exceed the
 * finite range when the mathematical result itself is too large. */
TrytrReal trytr_vector2_dot(TrytrVector2 left, TrytrVector2 right);
TrytrReal trytr_vector2_cross(TrytrVector2 left, TrytrVector2 right);
TrytrReal trytr_vector2_length(TrytrVector2 value);
bool trytr_vector2_is_finite(TrytrVector2 value);
bool trytr_vector2_normalize(TrytrVector2 value, TrytrVector2 *out_unit);
bool trytr_vector2_rotate(TrytrVector2 value, TrytrReal angle_radians,
                          TrytrVector2 *out_rotated);

TrytrComplex trytr_vector2_to_complex(TrytrVector2 value);
TrytrVector2 trytr_vector2_from_complex(TrytrComplex value);

#endif
