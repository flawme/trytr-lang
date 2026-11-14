#include "trytr/math/matrix2.h"

#include <math.h>
#include <stddef.h>

TrytrMatrix2 trytr_matrix2_make(TrytrReal m00, TrytrReal m01,
                                TrytrReal m10, TrytrReal m11)
{
    return (TrytrMatrix2){
        .m00 = m00, .m01 = m01, .m10 = m10, .m11 = m11
    };
}

TrytrMatrix2 trytr_matrix2_identity(void)
{
    return trytr_matrix2_make(1.0, 0.0, 0.0, 1.0);
}

static TrytrReal compensated_determinant(TrytrReal a, TrytrReal b,
                                        TrytrReal c, TrytrReal d)
{
    const TrytrReal ad = a * d;
    const TrytrReal ad_error = fma(a, d, -ad);
    const TrytrReal bc = b * c;
    const TrytrReal bc_error = fma(b, c, -bc);
    return (ad - bc) + (ad_error - bc_error);
}

static TrytrReal determinant_with_row_scales(TrytrReal a, TrytrReal b,
                                             TrytrReal c, TrytrReal d,
                                             TrytrReal row0_scale,
                                             TrytrReal row1_scale)
{
    int row0_exponent = 0;
    int row1_exponent = 0;
    const TrytrReal row0_mantissa = frexp(row0_scale, &row0_exponent);
    const TrytrReal row1_mantissa = frexp(row1_scale, &row1_exponent);
    const TrytrReal normalized = compensated_determinant(
        a / row0_scale, b / row0_scale, c / row1_scale, d / row1_scale);
    if (normalized == 0.0) return 0.0;

    int determinant_exponent = 0;
    const TrytrReal determinant_mantissa = frexp(normalized,
                                                 &determinant_exponent);
    return scalbn(determinant_mantissa * row0_mantissa * row1_mantissa,
                  determinant_exponent + row0_exponent + row1_exponent);
}

static TrytrReal divide_by_determinant_and_scale(TrytrReal numerator,
                                                TrytrReal determinant,
                                                TrytrReal scale)
{
    if (numerator == 0.0) return 0.0;
    int numerator_exponent = 0;
    int determinant_exponent = 0;
    int scale_exponent = 0;
    const TrytrReal numerator_mantissa = frexp(numerator, &numerator_exponent);
    const TrytrReal determinant_mantissa = frexp(determinant,
                                                  &determinant_exponent);
    const TrytrReal scale_mantissa = frexp(scale, &scale_exponent);
    const TrytrReal quotient_mantissa =
        (numerator_mantissa / determinant_mantissa) / scale_mantissa;
    int adjustment = 0;
    const TrytrReal normalized_mantissa = frexp(quotient_mantissa, &adjustment);
    const int exponent = numerator_exponent - determinant_exponent -
                         scale_exponent + adjustment;
    return scalbn(normalized_mantissa, exponent);
}

TrytrReal trytr_matrix2_determinant(TrytrMatrix2 matrix)
{
    if (!trytr_matrix2_is_finite(matrix)) return NAN;
    const TrytrReal ad = matrix.m00 * matrix.m11;
    const TrytrReal bc = matrix.m01 * matrix.m10;
    const bool ad_underflowed = ad == 0.0
        ? matrix.m00 != 0.0 && matrix.m11 != 0.0
        : fpclassify(ad) == FP_SUBNORMAL;
    const bool bc_underflowed = bc == 0.0
        ? matrix.m01 != 0.0 && matrix.m10 != 0.0
        : fpclassify(bc) == FP_SUBNORMAL;
    if (isfinite(ad) && isfinite(bc) && !ad_underflowed &&
        !bc_underflowed) {
        return compensated_determinant(matrix.m00, matrix.m01,
                                       matrix.m10, matrix.m11);
    }

    const TrytrReal row0_scale = fmax(fabs(matrix.m00), fabs(matrix.m01));
    const TrytrReal row1_scale = fmax(fabs(matrix.m10), fabs(matrix.m11));
    if (row0_scale == 0.0 || row1_scale == 0.0) return 0.0;
    return determinant_with_row_scales(matrix.m00, matrix.m01,
                                       matrix.m10, matrix.m11,
                                       row0_scale, row1_scale);
}

bool trytr_matrix2_is_finite(TrytrMatrix2 matrix)
{
    return trytr_real_is_finite(matrix.m00) &&
           trytr_real_is_finite(matrix.m01) &&
           trytr_real_is_finite(matrix.m10) &&
           trytr_real_is_finite(matrix.m11);
}

bool trytr_matrix2_multiply(TrytrMatrix2 left, TrytrMatrix2 right,
                            TrytrMatrix2 *out_product)
{
    if (out_product == NULL) return false;
    *out_product = trytr_matrix2_make(0.0, 0.0, 0.0, 0.0);
    if (!trytr_matrix2_is_finite(left) || !trytr_matrix2_is_finite(right))
        return false;

    const TrytrMatrix2 product = trytr_matrix2_make(
        left.m00 * right.m00 + left.m01 * right.m10,
        left.m00 * right.m01 + left.m01 * right.m11,
        left.m10 * right.m00 + left.m11 * right.m10,
        left.m10 * right.m01 + left.m11 * right.m11);
    if (!trytr_matrix2_is_finite(product)) return false;
    *out_product = product;
    return true;
}

bool trytr_matrix2_apply(TrytrMatrix2 matrix, TrytrVector2 vector,
                         TrytrVector2 *out_vector)
{
    if (out_vector == NULL) return false;
    *out_vector = trytr_vector2_make(0.0, 0.0);
    if (!trytr_matrix2_is_finite(matrix) || !trytr_vector2_is_finite(vector))
        return false;

    const TrytrVector2 result = trytr_vector2_make(
        matrix.m00 * vector.x + matrix.m01 * vector.y,
        matrix.m10 * vector.x + matrix.m11 * vector.y);
    if (!trytr_vector2_is_finite(result)) return false;
    *out_vector = result;
    return true;
}

bool trytr_matrix2_inverse(TrytrMatrix2 matrix, TrytrMatrix2 *out_inverse)
{
    if (out_inverse == NULL) return false;
    *out_inverse = trytr_matrix2_make(0.0, 0.0, 0.0, 0.0);
    if (!trytr_matrix2_is_finite(matrix)) return false;

    const TrytrReal row0_scale = fmax(fabs(matrix.m00), fabs(matrix.m01));
    const TrytrReal row1_scale = fmax(fabs(matrix.m10), fabs(matrix.m11));
    if (row0_scale == 0.0 || row1_scale == 0.0) return false;

    const TrytrReal a = matrix.m00 / row0_scale;
    const TrytrReal b = matrix.m01 / row0_scale;
    const TrytrReal c = matrix.m10 / row1_scale;
    const TrytrReal d = matrix.m11 / row1_scale;
    const TrytrReal determinant = compensated_determinant(a, b, c, d);
    if (determinant == 0.0 || !trytr_real_is_finite(determinant)) return false;

    const TrytrMatrix2 inverse = trytr_matrix2_make(
        divide_by_determinant_and_scale(d, determinant, row0_scale),
        divide_by_determinant_and_scale(-b, determinant, row1_scale),
        divide_by_determinant_and_scale(-c, determinant, row0_scale),
        divide_by_determinant_and_scale(a, determinant, row1_scale));
    if (!trytr_matrix2_is_finite(inverse)) return false;
    *out_inverse = inverse;
    return true;
}
