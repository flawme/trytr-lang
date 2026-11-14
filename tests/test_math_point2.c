#include "test.h"

#include "trytr/math/point2.h"

#include <math.h>

static bool test_point_vector_affine_operations(void)
{
    const TrytrPoint2 origin = trytr_point2_make(2.0, 3.0);
    const TrytrPoint2 target = trytr_point2_make(5.0, 7.0);
    const TrytrVector2 displacement = trytr_point2_difference(target, origin);
    const TrytrPoint2 translated = trytr_point2_translate(origin, displacement);

    TEST_ASSERT(displacement.x == 3.0);
    TEST_ASSERT(displacement.y == 4.0);
    TEST_ASSERT(translated.x == target.x);
    TEST_ASSERT(translated.y == target.y);
    TEST_ASSERT(trytr_point2_is_finite(target));
    TEST_ASSERT(!trytr_point2_is_finite(trytr_point2_make(INFINITY, 0.0)));
    return true;
}

static const TrytrTestCase tests[] = {
    {"point and vector semantics", test_point_vector_affine_operations}
};

TEST_MAIN(tests)
