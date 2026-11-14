#include "trytr/math/complex.h"

#include <math.h>
#include <stddef.h>

TrytrComplex trytr_complex_make(TrytrReal real, TrytrReal imaginary)
{
    return (TrytrComplex){.real = real, .imaginary = imaginary};
}

TrytrComplex trytr_complex_add(TrytrComplex left, TrytrComplex right)
{
    return trytr_complex_make(left.real + right.real,
                              left.imaginary + right.imaginary);
}

TrytrComplex trytr_complex_subtract(TrytrComplex left, TrytrComplex right)
{
    return trytr_complex_make(left.real - right.real,
                              left.imaginary - right.imaginary);
}

TrytrComplex trytr_complex_multiply(TrytrComplex left, TrytrComplex right)
{
    return trytr_complex_make(left.real * right.real -
                                  left.imaginary * right.imaginary,
                              left.real * right.imaginary +
                                  left.imaginary * right.real);
}

TrytrComplex trytr_complex_conjugate(TrytrComplex value)
{
    return trytr_complex_make(value.real, -value.imaginary);
}

TrytrReal trytr_complex_abs(TrytrComplex value)
{
    return trytr_real_hypot(value.real, value.imaginary);
}

bool trytr_complex_is_finite(TrytrComplex value)
{
    return trytr_real_is_finite(value.real) &&
           trytr_real_is_finite(value.imaginary);
}

bool trytr_complex_sqrt(TrytrComplex value, TrytrComplex *out_root)
{
    if (out_root == NULL) return false;
    *out_root = trytr_complex_make(0.0, 0.0);
    if (!trytr_complex_is_finite(value)) return false;

    const TrytrReal scale = fmax(fabs(value.real), fabs(value.imaginary));
    if (scale == 0.0) {
        *out_root = trytr_complex_make(0.0, copysign(0.0, value.imaginary));
        return true;
    }

    int exponent = 0;
    (void)frexp(scale, &exponent);
    if (exponent % 2 != 0) --exponent;
    const TrytrReal real = scalbn(value.real, -exponent);
    const TrytrReal imaginary = scalbn(value.imaginary, -exponent);
    const TrytrReal magnitude = trytr_real_hypot(real, imaginary);

    TrytrReal root_real;
    TrytrReal root_imaginary;
    if (real >= 0.0) {
        root_real = trytr_real_sqrt((magnitude + real) * 0.5);
        root_imaginary = imaginary / (2.0 * root_real);
    } else {
        root_imaginary = copysign(
            trytr_real_sqrt((magnitude - real) * 0.5), imaginary);
        root_real = root_imaginary == 0.0
            ? 0.0
            : fabs(imaginary) / (2.0 * fabs(root_imaginary));
    }

    const int root_exponent = exponent / 2;
    const TrytrComplex root = trytr_complex_make(
        scalbn(root_real, root_exponent),
        scalbn(root_imaginary, root_exponent));
    if (!trytr_complex_is_finite(root)) return false;
    *out_root = root;
    return true;
}

bool trytr_complex_divide(TrytrComplex numerator, TrytrComplex denominator,
                          TrytrComplex *out_quotient)
{
    if (out_quotient == NULL) return false;
    *out_quotient = trytr_complex_make(0.0, 0.0);
    if (!trytr_complex_is_finite(numerator) ||
        !trytr_complex_is_finite(denominator) ||
        (denominator.real == 0.0 && denominator.imaginary == 0.0)) {
        return false;
    }

    const TrytrReal numerator_scale =
        fmax(fabs(numerator.real), fabs(numerator.imaginary));
    if (numerator_scale == 0.0) return true;
    const TrytrReal denominator_scale =
        fmax(fabs(denominator.real), fabs(denominator.imaginary));

    int numerator_exponent = 0;
    int denominator_exponent = 0;
    (void)frexp(numerator_scale, &numerator_exponent);
    (void)frexp(denominator_scale, &denominator_exponent);
    const TrytrReal ar = scalbn(numerator.real, -numerator_exponent);
    const TrytrReal ai = scalbn(numerator.imaginary, -numerator_exponent);
    const TrytrReal br = scalbn(denominator.real, -denominator_exponent);
    const TrytrReal bi = scalbn(denominator.imaginary, -denominator_exponent);

    /* Both complex values are normalized before the quotient is formed. */
    const TrytrReal denominator_norm = br * br + bi * bi;
    const TrytrReal real_part = (ar * br + ai * bi) / denominator_norm;
    const TrytrReal imaginary_part =
        (ai * br - ar * bi) / denominator_norm;
    const int result_exponent = numerator_exponent - denominator_exponent;
    const TrytrComplex quotient = trytr_complex_make(
        scalbn(real_part, result_exponent),
        scalbn(imaginary_part, result_exponent));
    if (!trytr_complex_is_finite(quotient)) return false;

    *out_quotient = quotient;
    return true;
}

bool trytr_complex_from_polar(TrytrReal radius, TrytrReal angle_radians,
                              TrytrComplex *out_value)
{
    if (out_value == NULL) return false;
    *out_value = trytr_complex_make(0.0, 0.0);
    if (!trytr_real_is_finite(radius) || radius < 0.0 ||
        !trytr_real_is_finite(angle_radians)) {
        return false;
    }

    const TrytrComplex value = trytr_complex_make(
        radius * trytr_real_cos(angle_radians),
        radius * trytr_real_sin(angle_radians));
    if (!trytr_complex_is_finite(value)) return false;
    *out_value = value;
    return true;
}
