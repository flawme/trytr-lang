#include "trytr/math/vector2.h"

#include <math.h>
#include <stddef.h>

typedef struct ScaledProduct {
    double high;
    double low;
    int exponent;
} ScaledProduct;

static ScaledProduct scaled_product(double left, double right)
{
    if (left == 0.0 || right == 0.0)
        return (ScaledProduct){.high = 0.0, .low = 0.0, .exponent = 0};

    int left_exponent = 0;
    int right_exponent = 0;
    const double left_fraction = frexp(left, &left_exponent);
    const double right_fraction = frexp(right, &right_exponent);
    const double product = left_fraction * right_fraction;
    const double product_error = fma(left_fraction, right_fraction, -product);
    int product_adjustment = 0;
    const double high = frexp(product, &product_adjustment);
    return (ScaledProduct){
        .high = high,
        .low = scalbn(product_error, -product_adjustment),
        .exponent = left_exponent + right_exponent + product_adjustment
    };
}

static double scaled_sum_of_products(double a, double b, double c, double d,
                                     double second_sign)
{
    const ScaledProduct first = scaled_product(a, b);
    const ScaledProduct second = scaled_product(c, d);
    if (first.high == 0.0 && second.high == 0.0) return 0.0;

    int common_exponent = first.high == 0.0
        ? second.exponent : first.exponent;
    if (second.high != 0.0 && second.exponent > common_exponent)
        common_exponent = second.exponent;

    const double first_high = first.high == 0.0
        ? 0.0 : scalbn(first.high, first.exponent - common_exponent);
    const double second_high = second.high == 0.0
        ? 0.0 : second_sign *
            scalbn(second.high, second.exponent - common_exponent);
    const double high_sum = first_high + second_high;
    const double virtual_second = high_sum - first_high;
    const double sum_error = (first_high - (high_sum - virtual_second)) +
                             (second_high - virtual_second);
    const double first_low = first.high == 0.0
        ? 0.0 : scalbn(first.low, first.exponent - common_exponent);
    const double second_low = second.high == 0.0
        ? 0.0 : second_sign *
            scalbn(second.low, second.exponent - common_exponent);
    const double normalized = high_sum + (sum_error + first_low + second_low);
    return scalbn(normalized, common_exponent);
}

static double sum_of_products(double a, double b, double c, double d,
                              double second_sign)
{
    if (!isfinite(a) || !isfinite(b) || !isfinite(c) || !isfinite(d)) {
        return a * b + second_sign * (c * d);
    }

    const double first_product = a * b;
    const double second_product = c * d;
    const bool first_underflowed = first_product == 0.0
        ? a != 0.0 && b != 0.0
        : fpclassify(first_product) == FP_SUBNORMAL;
    const bool second_underflowed = second_product == 0.0
        ? c != 0.0 && d != 0.0
        : fpclassify(second_product) == FP_SUBNORMAL;
    if (isfinite(first_product) && isfinite(second_product) &&
        !first_underflowed && !second_underflowed) {
        return fma(a, b, second_sign * second_product);
    }
    return scaled_sum_of_products(a, b, c, d, second_sign);
}

TrytrVector2 trytr_vector2_make(TrytrReal x, TrytrReal y)
{
    return (TrytrVector2){.x = x, .y = y};
}

TrytrVector2 trytr_vector2_add(TrytrVector2 left, TrytrVector2 right)
{
    return trytr_vector2_make(left.x + right.x, left.y + right.y);
}

TrytrVector2 trytr_vector2_subtract(TrytrVector2 left, TrytrVector2 right)
{
    return trytr_vector2_make(left.x - right.x, left.y - right.y);
}

TrytrVector2 trytr_vector2_scale(TrytrVector2 value, TrytrReal factor)
{
    return trytr_vector2_make(value.x * factor, value.y * factor);
}

TrytrReal trytr_vector2_dot(TrytrVector2 left, TrytrVector2 right)
{
    return sum_of_products(left.x, right.x, left.y, right.y, 1.0);
}

TrytrReal trytr_vector2_cross(TrytrVector2 left, TrytrVector2 right)
{
    return sum_of_products(left.x, right.y, left.y, right.x, -1.0);
}

TrytrReal trytr_vector2_length(TrytrVector2 value)
{
    return trytr_real_hypot(value.x, value.y);
}

bool trytr_vector2_is_finite(TrytrVector2 value)
{
    return trytr_real_is_finite(value.x) && trytr_real_is_finite(value.y);
}

bool trytr_vector2_normalize(TrytrVector2 value, TrytrVector2 *out_unit)
{
    if (out_unit == NULL) return false;
    *out_unit = trytr_vector2_make(0.0, 0.0);
    if (!trytr_vector2_is_finite(value)) return false;

    const TrytrReal scale = fmax(fabs(value.x), fabs(value.y));
    if (scale == 0.0) return false;
    const TrytrReal scaled_x = value.x / scale;
    const TrytrReal scaled_y = value.y / scale;
    const TrytrReal scaled_length = trytr_real_hypot(scaled_x, scaled_y);
    *out_unit = trytr_vector2_make(scaled_x / scaled_length,
                                   scaled_y / scaled_length);
    return true;
}

bool trytr_vector2_rotate(TrytrVector2 value, TrytrReal angle_radians,
                          TrytrVector2 *out_rotated)
{
    if (out_rotated == NULL) return false;
    *out_rotated = trytr_vector2_make(0.0, 0.0);
    if (!trytr_vector2_is_finite(value) ||
        !trytr_real_is_finite(angle_radians)) {
        return false;
    }

    const TrytrReal value_scale = fmax(fabs(value.x), fabs(value.y));
    if (value_scale == 0.0) return true;

    TrytrComplex rotation;
    if (!trytr_complex_from_polar(1.0, angle_radians, &rotation)) return false;
    const TrytrComplex scaled_value = trytr_complex_make(
        value.x / value_scale, value.y / value_scale);
    const TrytrComplex rotated = trytr_complex_multiply(scaled_value, rotation);
    const TrytrVector2 result = trytr_vector2_make(
        rotated.real * value_scale, rotated.imaginary * value_scale);
    if (!trytr_vector2_is_finite(result)) return false;
    *out_rotated = result;
    return true;
}

TrytrComplex trytr_vector2_to_complex(TrytrVector2 value)
{
    return trytr_complex_make(value.x, value.y);
}

TrytrVector2 trytr_vector2_from_complex(TrytrComplex value)
{
    return trytr_vector2_make(value.real, value.imaginary);
}
