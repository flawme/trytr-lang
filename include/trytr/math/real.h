#ifndef TRYTR_MATH_REAL_H
#define TRYTR_MATH_REAL_H

#include <stdbool.h>

/* The scalar type used by the built-in numerical modules. */
typedef double TrytrReal;

#define TRYTR_REAL_PI 3.14159265358979323846264338327950288
#define TRYTR_REAL_E 2.71828182845904523536028747135266250

bool trytr_real_is_finite(TrytrReal value);
TrytrReal trytr_real_sqrt(TrytrReal value);
TrytrReal trytr_real_hypot(TrytrReal x, TrytrReal y);
TrytrReal trytr_real_sin(TrytrReal radians);
TrytrReal trytr_real_cos(TrytrReal radians);
TrytrReal trytr_real_tan(TrytrReal radians);
TrytrReal trytr_real_asin(TrytrReal value);
TrytrReal trytr_real_acos(TrytrReal value);
TrytrReal trytr_real_atan(TrytrReal value);
TrytrReal trytr_real_atan2(TrytrReal y, TrytrReal x);
TrytrReal trytr_real_exp(TrytrReal value);
TrytrReal trytr_real_log(TrytrReal value);
TrytrReal trytr_real_log10(TrytrReal value);
TrytrReal trytr_real_pow(TrytrReal base, TrytrReal exponent);
TrytrReal trytr_real_sinh(TrytrReal value);
TrytrReal trytr_real_cosh(TrytrReal value);
TrytrReal trytr_real_tanh(TrytrReal value);

#endif
