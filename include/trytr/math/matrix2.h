#ifndef TRYTR_MATH_MATRIX2_H
#define TRYTR_MATH_MATRIX2_H

#include "trytr/math/vector2.h"

#include <stdbool.h>

/* A row-major 2 by 2 linear map; translation belongs to affine transforms. */
typedef struct TrytrMatrix2 {
    TrytrReal m00;
    TrytrReal m01;
    TrytrReal m10;
    TrytrReal m11;
} TrytrMatrix2;

TrytrMatrix2 trytr_matrix2_make(TrytrReal m00, TrytrReal m01,
                                TrytrReal m10, TrytrReal m11);
TrytrMatrix2 trytr_matrix2_identity(void);
TrytrReal trytr_matrix2_determinant(TrytrMatrix2 matrix);
bool trytr_matrix2_is_finite(TrytrMatrix2 matrix);
bool trytr_matrix2_multiply(TrytrMatrix2 left, TrytrMatrix2 right,
                            TrytrMatrix2 *out_product);
bool trytr_matrix2_apply(TrytrMatrix2 matrix, TrytrVector2 vector,
                         TrytrVector2 *out_vector);
bool trytr_matrix2_inverse(TrytrMatrix2 matrix, TrytrMatrix2 *out_inverse);

#endif
