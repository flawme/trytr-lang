#include "test.h"

#include "trytr/math/real.h"

#include <math.h>

static bool close_to(double actual, double expected)
{
    return fabs(actual - expected) < 1e-12;
}

static bool test_real_primitives(void)
{
    TEST_ASSERT(trytr_real_is_finite(2.0));
    TEST_ASSERT(!trytr_real_is_finite(INFINITY));
    TEST_ASSERT(!trytr_real_is_finite(NAN));
    TEST_ASSERT(close_to(trytr_real_sqrt(9.0), 3.0));
    TEST_ASSERT(isnan(trytr_real_sqrt(-1.0)));
    TEST_ASSERT(close_to(trytr_real_hypot(3.0, 4.0), 5.0));
    TEST_ASSERT(close_to(trytr_real_hypot(1e308, 1e308),
                         sqrt(2.0) * 1e308));
    TEST_ASSERT(close_to(trytr_real_sin(TRYTR_REAL_PI / 2.0), 1.0));
    TEST_ASSERT(close_to(trytr_real_cos(TRYTR_REAL_PI), -1.0));
    TEST_ASSERT(close_to(trytr_real_atan2(1.0, 0.0), TRYTR_REAL_PI / 2.0));
    return true;
}

static const TrytrTestCase tests[] = {
    {"real scalar primitives", test_real_primitives}
};

TEST_MAIN(tests)
