#ifndef TRYTR_MATH_REAL_H
#define TRYTR_MATH_REAL_H

#include <stdbool.h>

/* The scalar type used by the built-in numerical modules. */
typedef double TrytrReal;

#define TRYTR_REAL_PI 3.14159265358979323846264338327950288

bool trytr_real_is_finite(TrytrReal value);
TrytrReal trytr_real_sqrt(TrytrReal value);
TrytrReal trytr_real_hypot(TrytrReal x, TrytrReal y);
TrytrReal trytr_real_sin(TrytrReal radians);
TrytrReal trytr_real_cos(TrytrReal radians);
TrytrReal trytr_real_atan2(TrytrReal y, TrytrReal x);

#endif
