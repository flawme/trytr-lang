#include "test.h"

#include "trytr/math/affine2.h"

#include <math.h>

static bool close_to(double actual, double expected)
{
    return fabs(actual - expected) < 1e-10;
}

static bool relative_to(double actual, double expected)
{
    return expected != 0.0 && fabs(actual / expected - 1.0) < 1e-12;
}

static bool test_affine_composition_order_and_inverse(void)
{
    TrytrAffine2 rotation;
    TrytrAffine2 combined;
    TrytrAffine2 inverse;
    TEST_ASSERT(trytr_affine2_rotation(TRYTR_REAL_PI / 2.0, &rotation));
    const TrytrAffine2 translation =
        trytr_affine2_translation(trytr_vector2_make(2.0, 0.0));
    TEST_ASSERT(trytr_affine2_compose(rotation, translation, &combined));

    TrytrPoint2 point;
    TEST_ASSERT(trytr_affine2_apply(combined, trytr_point2_make(1.0, 0.0),
                                    &point));
    TEST_ASSERT(close_to(point.x, 0.0));
    TEST_ASSERT(close_to(point.y, 3.0));
    TEST_ASSERT(trytr_affine2_inverse(combined, &inverse));
    TEST_ASSERT(trytr_affine2_apply(inverse, point, &point));
    TEST_ASSERT(close_to(point.x, 1.0));
    TEST_ASSERT(close_to(point.y, 0.0));
    return true;
}

static bool test_affine_transform_with_extreme_axis_scales(void)
{
    const TrytrAffine2 scale = trytr_affine2_scaling(1e308, 1e-308);
    TrytrPoint2 mapped;
    TrytrAffine2 inverse;
    TrytrPoint2 restored;

    TEST_ASSERT(trytr_affine2_apply(
        scale, trytr_point2_make(1e-308, 1e308), &mapped));
    TEST_ASSERT(close_to(mapped.x, 1.0));
    TEST_ASSERT(close_to(mapped.y, 1.0));
    TEST_ASSERT(trytr_affine2_inverse(scale, &inverse));
    TEST_ASSERT(trytr_affine2_apply(inverse, mapped, &restored));
    TEST_ASSERT(relative_to(restored.x, 1e-308));
    TEST_ASSERT(relative_to(restored.y, 1e308));
    return true;
}

static bool test_affine_invalid_transform_fails_cleanly(void)
{
    TrytrAffine2 rotation;
    TEST_ASSERT(!trytr_affine2_rotation(INFINITY, &rotation));
    TrytrAffine2 inverse = trytr_affine2_identity();
    TEST_ASSERT(!trytr_affine2_inverse(trytr_affine2_scaling(0.0, 1.0),
                                       &inverse));
    TEST_ASSERT(inverse.linear.m00 == 0.0);
    TEST_ASSERT(inverse.linear.m11 == 0.0);
    TEST_ASSERT(inverse.translation.x == 0.0);
    TEST_ASSERT(inverse.translation.y == 0.0);
    return true;
}

static const TrytrTestCase tests[] = {
    {"affine composition and inverse", test_affine_composition_order_and_inverse},
    {"extreme affine scales", test_affine_transform_with_extreme_axis_scales},
    {"invalid affine operations", test_affine_invalid_transform_fails_cleanly}
};

TEST_MAIN(tests)
