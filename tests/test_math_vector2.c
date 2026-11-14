#include "test.h"

#include "trytr/math/vector2.h"

#include <float.h>
#include <math.h>

static bool close_to(double actual, double expected)
{
    return fabs(actual - expected) < 1e-10;
}

static bool test_vector_products_and_complex_bridge(void)
{
    const TrytrVector2 left = trytr_vector2_make(3.0, 4.0);
    const TrytrVector2 right = trytr_vector2_make(-4.0, 3.0);
    const TrytrComplex complex = trytr_vector2_to_complex(left);
    const TrytrVector2 restored = trytr_vector2_from_complex(complex);

    TEST_ASSERT(close_to(trytr_vector2_dot(left, right), 0.0));
    TEST_ASSERT(close_to(trytr_vector2_cross(left, right), 25.0));
    TEST_ASSERT(close_to(trytr_vector2_length(left), 5.0));
    TEST_ASSERT(close_to(restored.x, left.x));
    TEST_ASSERT(close_to(restored.y, left.y));
    return true;
}

static bool test_vector_normalize_and_rotate(void)
{
    TrytrVector2 result;
    TEST_ASSERT(trytr_vector2_normalize(trytr_vector2_make(DBL_MAX, DBL_MAX),
                                        &result));
    TEST_ASSERT(close_to(result.x, sqrt(0.5)));
    TEST_ASSERT(close_to(result.y, sqrt(0.5)));
    TEST_ASSERT(trytr_vector2_rotate(trytr_vector2_make(2.0, 0.0),
                                     TRYTR_REAL_PI / 2.0, &result));
    TEST_ASSERT(close_to(result.x, 0.0));
    TEST_ASSERT(close_to(result.y, 2.0));
    TEST_ASSERT(!trytr_vector2_normalize(trytr_vector2_make(0.0, 0.0),
                                         &result));
    TEST_ASSERT(!trytr_vector2_rotate(trytr_vector2_make(1.0, 0.0), INFINITY,
                                      &result));
    TEST_ASSERT(close_to(result.x, 0.0));
    TEST_ASSERT(close_to(result.y, 0.0));
    TEST_ASSERT(!trytr_vector2_rotate(trytr_vector2_make(DBL_MAX, DBL_MAX),
                                      TRYTR_REAL_PI / 4.0, &result));
    return true;
}

static bool test_vector_products_survive_overflow_cancellation(void)
{
    const TrytrVector2 diagonal = trytr_vector2_make(DBL_MAX, DBL_MAX);
    const TrytrVector2 opposite = trytr_vector2_make(DBL_MAX, -DBL_MAX);
    const TrytrVector2 almost_scaled =
        trytr_vector2_make(2.0, nextafter(2.0, 0.0));

    TEST_ASSERT(trytr_vector2_dot(diagonal, opposite) == 0.0);
    TEST_ASSERT(trytr_vector2_cross(diagonal, diagonal) == 0.0);
    TEST_ASSERT(trytr_vector2_cross(diagonal, almost_scaled) ==
                -scalbn(DBL_MAX, -52));
    const double smallest = nextafter(0.0, 1.0);
    TEST_ASSERT(trytr_vector2_dot(trytr_vector2_make(smallest, smallest),
                                  trytr_vector2_make(0.5, 0.5)) == smallest);
    return true;
}

static const TrytrTestCase tests[] = {
    {"vector products and complex bridge", test_vector_products_and_complex_bridge},
    {"stable normalization and rotation", test_vector_normalize_and_rotate},
    {"scaled vector product cancellation",
     test_vector_products_survive_overflow_cancellation}
};

TEST_MAIN(tests)
