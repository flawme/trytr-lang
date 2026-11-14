#ifndef TRYTR_MATH_COMPLEX_H
#define TRYTR_MATH_COMPLEX_H

#include "trytr/math/real.h"

#include <stdbool.h>

/* Explicit Cartesian storage keeps the ABI and component access predictable. */
typedef struct TrytrComplex {
    TrytrReal real;
    TrytrReal imaginary;
} TrytrComplex;

TrytrComplex trytr_complex_make(TrytrReal real, TrytrReal imaginary);
TrytrComplex trytr_complex_add(TrytrComplex left, TrytrComplex right);
TrytrComplex trytr_complex_subtract(TrytrComplex left, TrytrComplex right);
TrytrComplex trytr_complex_multiply(TrytrComplex left, TrytrComplex right);
TrytrComplex trytr_complex_conjugate(TrytrComplex value);
TrytrReal trytr_complex_abs(TrytrComplex value);
bool trytr_complex_is_finite(TrytrComplex value);

/* Returns the principal square root; preserves the side of the negative-real
 * branch cut using the sign of the input imaginary zero. */
bool trytr_complex_sqrt(TrytrComplex value, TrytrComplex *out_root);

/* Division fails for non-finite input, zero divisor, or unrepresentable result. */
bool trytr_complex_divide(TrytrComplex numerator, TrytrComplex denominator,
                          TrytrComplex *out_quotient);

/* Angles are in radians; radius must be finite and non-negative. */
bool trytr_complex_from_polar(TrytrReal radius, TrytrReal angle_radians,
                              TrytrComplex *out_value);

#endif
