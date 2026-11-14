#include "test.h"

#include "trytr/math/matrix2.h"

#include <float.h>
#include <math.h>

static bool close_to(double actual, double expected)
{
    return fabs(actual - expected) < 1e-10;
}

static bool test_matrix_apply_multiply_and_inverse(void)
{
    const TrytrMatrix2 matrix = trytr_matrix2_make(2.0, 1.0, 3.0, 2.0);
    const TrytrVector2 vector = trytr_vector2_make(4.0, 5.0);
    TrytrVector2 transformed;
    TrytrMatrix2 inverse;
    TrytrMatrix2 product;

    TEST_ASSERT(trytr_matrix2_apply(matrix, vector, &transformed));
    TEST_ASSERT(close_to(transformed.x, 13.0));
    TEST_ASSERT(close_to(transformed.y, 22.0));
    TEST_ASSERT(close_to(trytr_matrix2_determinant(matrix), 1.0));
    TEST_ASSERT(trytr_matrix2_inverse(matrix, &inverse));
    TEST_ASSERT(trytr_matrix2_multiply(matrix, inverse, &product));
    TEST_ASSERT(close_to(product.m00, 1.0));
    TEST_ASSERT(close_to(product.m01, 0.0));
    TEST_ASSERT(close_to(product.m10, 0.0));
    TEST_ASSERT(close_to(product.m11, 1.0));
    return true;
}

static bool test_matrix_inverse_handles_scale_and_failure(void)
{
    TrytrMatrix2 inverse;
    TEST_ASSERT(close_to(trytr_matrix2_determinant(
        trytr_matrix2_make(DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX)), 0.0));
    TEST_ASSERT(trytr_matrix2_inverse(
        trytr_matrix2_make(1e308, 0.0, 0.0, 1e308), &inverse));
    TEST_ASSERT(close_to(inverse.m00, 1e-308));
    TEST_ASSERT(close_to(inverse.m11, 1e-308));

    inverse = trytr_matrix2_identity();
    TEST_ASSERT(!trytr_matrix2_inverse(
        trytr_matrix2_make(1.0, 2.0, 2.0, 4.0), &inverse));
    TEST_ASSERT(close_to(inverse.m00, 0.0));
    TEST_ASSERT(close_to(inverse.m11, 0.0));
    TEST_ASSERT(!trytr_matrix2_apply(trytr_matrix2_identity(),
                                     trytr_vector2_make(INFINITY, 0.0), NULL));
    return true;
}

static bool test_matrix_extreme_row_scales_preserve_invertibility(void)
{
    const TrytrMatrix2 matrix = trytr_matrix2_make(1e308, 0.0, 0.0, 1e-308);
    TrytrMatrix2 inverse;

    TEST_ASSERT(close_to(trytr_matrix2_determinant(matrix), 1.0));
    TEST_ASSERT(trytr_matrix2_inverse(matrix, &inverse));
    TEST_ASSERT(close_to(inverse.m00 / 1e-308, 1.0));
    TEST_ASSERT(close_to(inverse.m01, 0.0));
    TEST_ASSERT(close_to(inverse.m10, 0.0));
    TEST_ASSERT(close_to(inverse.m11 / 1e308, 1.0));

    for (int exponent = -1000; exponent <= 1000; exponent += 100) {
        const double large = scalbn(1.0, exponent);
        const double small = scalbn(1.0, -exponent);
        const TrytrMatrix2 balanced = trytr_matrix2_make(large, 0.0,
                                                        0.0, small);
        TEST_ASSERT(close_to(trytr_matrix2_determinant(balanced), 1.0));
        TEST_ASSERT(trytr_matrix2_inverse(balanced, &inverse));
        TEST_ASSERT(close_to(inverse.m00 / small, 1.0));
        TEST_ASSERT(close_to(inverse.m11 / large, 1.0));
    }
    return true;
}

static bool test_matrix_near_singular_exact_difference_remains_invertible(void)
{
    const double adjacent = nextafter(1.0, 2.0);
    const double gap = adjacent - 1.0;
    const TrytrMatrix2 matrix = trytr_matrix2_make(1.0, 1.0, 1.0, adjacent);
    TrytrMatrix2 inverse;
    TrytrMatrix2 identity;

    TEST_ASSERT(trytr_matrix2_determinant(matrix) == gap);
    TEST_ASSERT(trytr_matrix2_inverse(matrix, &inverse));
    TEST_ASSERT(trytr_matrix2_multiply(matrix, inverse, &identity));
    TEST_ASSERT(close_to(identity.m00, 1.0));
    TEST_ASSERT(close_to(identity.m01, 0.0));
    TEST_ASSERT(close_to(identity.m10, 0.0));
    TEST_ASSERT(close_to(identity.m11, 1.0));
    return true;
}

static const TrytrTestCase tests[] = {
    {"matrix operations", test_matrix_apply_multiply_and_inverse},
    {"scaled matrix inverse and errors", test_matrix_inverse_handles_scale_and_failure},
    {"extreme row scaling", test_matrix_extreme_row_scales_preserve_invertibility},
    {"near singular matrix precision",
     test_matrix_near_singular_exact_difference_remains_invertible}
};

TEST_MAIN(tests)
