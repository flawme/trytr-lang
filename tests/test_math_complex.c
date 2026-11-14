#include "test.h"

#include "trytr/math/complex.h"

#include <float.h>
#include <math.h>

static bool close_to(double actual, double expected)
{
    return fabs(actual - expected) < 1e-12;
}

static bool test_complex_algebra_and_polar_form(void)
{
    const TrytrComplex value = trytr_complex_make(3.0, 4.0);
    const TrytrComplex conjugate = trytr_complex_conjugate(value);
    const TrytrComplex square = trytr_complex_multiply(value, value);
    TrytrComplex polar;

    TEST_ASSERT(close_to(trytr_complex_abs(value), 5.0));
    TEST_ASSERT(close_to(conjugate.real, 3.0));
    TEST_ASSERT(close_to(conjugate.imaginary, -4.0));
    TEST_ASSERT(close_to(square.real, -7.0));
    TEST_ASSERT(close_to(square.imaginary, 24.0));
    TEST_ASSERT(trytr_complex_is_finite(value));
    TEST_ASSERT(trytr_complex_from_polar(2.0, TRYTR_REAL_PI / 2.0, &polar));
    TEST_ASSERT(close_to(polar.real, 0.0));
    TEST_ASSERT(close_to(polar.imaginary, 2.0));
    TEST_ASSERT(!trytr_complex_from_polar(-1.0, 0.0, &polar));
    TEST_ASSERT(close_to(polar.real, 0.0));
    TEST_ASSERT(close_to(polar.imaginary, 0.0));
    return true;
}

static bool test_complex_division_scales_before_products(void)
{
    TrytrComplex quotient;
    const TrytrComplex huge = trytr_complex_make(1e308, 1e308);
    const TrytrComplex tiny = trytr_complex_make(1e-308, 1e-308);

    TEST_ASSERT(trytr_complex_divide(huge, huge, &quotient));
    TEST_ASSERT(close_to(quotient.real, 1.0));
    TEST_ASSERT(close_to(quotient.imaginary, 0.0));
    TEST_ASSERT(trytr_complex_divide(tiny, tiny, &quotient));
    TEST_ASSERT(close_to(quotient.real, 1.0));
    TEST_ASSERT(close_to(quotient.imaginary, 0.0));
    TEST_ASSERT(trytr_complex_divide(trytr_complex_make(7.0, -5.0),
                                     trytr_complex_make(2.0, 1.0), &quotient));
    TEST_ASSERT(close_to(quotient.real, 1.8));
    TEST_ASSERT(close_to(quotient.imaginary, -3.4));
    return true;
}

static bool test_complex_division_failure_resets_output(void)
{
    TrytrComplex quotient = trytr_complex_make(9.0, 9.0);
    TEST_ASSERT(!trytr_complex_divide(trytr_complex_make(1.0, 2.0),
                                      trytr_complex_make(0.0, -0.0), &quotient));
    TEST_ASSERT(close_to(quotient.real, 0.0));
    TEST_ASSERT(close_to(quotient.imaginary, 0.0));
    TEST_ASSERT(!trytr_complex_divide(trytr_complex_make(INFINITY, 1.0),
                                      trytr_complex_make(1.0, 0.0), &quotient));
    TEST_ASSERT(!trytr_complex_divide(trytr_complex_make(1.0, 0.0),
                                      trytr_complex_make(1e-309, 0.0), &quotient));
    TEST_ASSERT(!trytr_complex_divide(trytr_complex_make(1.0, 0.0),
                                      trytr_complex_make(1.0, 0.0), NULL));
    return true;
}

static bool test_complex_square_root_principal_branch(void)
{
    TrytrComplex root;
    TEST_ASSERT(trytr_complex_sqrt(trytr_complex_make(3.0, 4.0), &root));
    TEST_ASSERT(close_to(root.real, 2.0));
    TEST_ASSERT(close_to(root.imaginary, 1.0));

    TEST_ASSERT(trytr_complex_sqrt(trytr_complex_make(-4.0, 0.0), &root));
    TEST_ASSERT(close_to(root.real, 0.0));
    TEST_ASSERT(close_to(root.imaginary, 2.0));
    TEST_ASSERT(trytr_complex_sqrt(trytr_complex_make(-4.0, -0.0), &root));
    TEST_ASSERT(close_to(root.real, 0.0));
    TEST_ASSERT(close_to(root.imaginary, -2.0));
    return true;
}

static bool test_complex_square_root_handles_extreme_scales(void)
{
    TrytrComplex root;
    const double root_scale = sqrt(DBL_MAX);
    TEST_ASSERT(trytr_complex_sqrt(trytr_complex_make(DBL_MAX, DBL_MAX),
                                   &root));
    TEST_ASSERT(isfinite(root.real) && isfinite(root.imaginary));
    TEST_ASSERT(fabs(root.real / root_scale - sqrt((sqrt(2.0) + 1.0) / 2.0))
                < 1e-14);
    TEST_ASSERT(fabs(root.imaginary / root_scale -
                     sqrt((sqrt(2.0) - 1.0) / 2.0)) < 1e-14);

    TEST_ASSERT(trytr_complex_sqrt(trytr_complex_make(1e-308, 0.0), &root));
    TEST_ASSERT(fabs(root.real / 1e-154 - 1.0) < 1e-14);
    TEST_ASSERT(root.imaginary == 0.0);
    TEST_ASSERT(trytr_complex_sqrt(
        trytr_complex_make(nextafter(0.0, 1.0), 0.0), &root));
    TEST_ASSERT(root.real > 0.0 && isfinite(root.real));
    return true;
}

static bool test_complex_square_root_rejects_nonfinite_input(void)
{
    TrytrComplex root = trytr_complex_make(9.0, 9.0);
    TEST_ASSERT(!trytr_complex_sqrt(trytr_complex_make(NAN, 1.0), &root));
    TEST_ASSERT(root.real == 0.0 && root.imaginary == 0.0);
    TEST_ASSERT(!trytr_complex_sqrt(trytr_complex_make(1.0, 1.0), NULL));
    return true;
}

static const TrytrTestCase tests[] = {
    {"complex algebra and polar form", test_complex_algebra_and_polar_form},
    {"scaled complex division", test_complex_division_scales_before_products},
    {"complex division errors", test_complex_division_failure_resets_output},
    {"complex square root principal branch", test_complex_square_root_principal_branch},
    {"complex square root extreme scales", test_complex_square_root_handles_extreme_scales},
    {"complex square root errors", test_complex_square_root_rejects_nonfinite_input}
};

TEST_MAIN(tests)
