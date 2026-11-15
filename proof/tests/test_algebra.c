#include "trytr/algebra.h"

#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(condition)                                                   \
    do {                                                                         \
        if (!(condition)) {                                                       \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, \
                    #condition);                                                  \
            return false;                                                        \
        }                                                                        \
    } while (false)

static bool accepts_binomial_expansion(void)
{
    const TrytrAlgebraResult result =
        trytr_algebra_check("(x+y)^2 = x^2+2xy+y^2");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_VERIFIED);
    return true;
}

static bool accepts_multivariate_reordering(void)
{
    const TrytrAlgebraResult result =
        trytr_algebra_check("xyz + 3ab = 3ba + zxy");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_VERIFIED);
    return true;
}

static bool accepts_exact_rational_fractions(void)
{
    const TrytrAlgebraResult result = trytr_algebra_check(
        "\\frac{x+y}{2} = \\frac{x}{2} + \\frac{y}{2}");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_VERIFIED);
    return true;
}

static bool accepts_exact_decimal_coefficients(void)
{
    const TrytrAlgebraResult result =
        trytr_algebra_check("0.5x = \\frac{x}{2}");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_VERIFIED);
    return true;
}

static bool accepts_rational_cancellation(void)
{
    const TrytrAlgebraResult result =
        trytr_algebra_check("\\frac{1}{3}x + \\frac{1}{6}x = \\frac{1}{2}x");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_VERIFIED);
    return true;
}

static bool accepts_latex_variables_and_subscripts(void)
{
    const TrytrAlgebraResult result = trytr_algebra_check(
        "\\alpha x_1 + \\beta x_2 = x_2\\beta + x_1\\alpha");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_VERIFIED);
    return true;
}

static bool accepts_wrapped_display_equation(void)
{
    const TrytrAlgebraResult result = trytr_algebra_check(
        "\\[(a-b)(a+b)=a^2-b^2\\]");
    if (result.status != TRYTR_ALGEBRA_VERIFIED) {
        fprintf(stderr, "wrapped equation result: %s\n", result.message);
    }
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_VERIFIED);
    return true;
}

static bool accepts_square_and_brace_grouping(void)
{
    const TrytrAlgebraResult result =
        trytr_algebra_check("[x+y]^2 = {x^2+2xy+y^2}");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_VERIFIED);
    return true;
}

static bool rejects_non_identity(void)
{
    const TrytrAlgebraResult result = trytr_algebra_check("x^2 = x");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_NOT_IDENTITY);
    return true;
}

static bool rejects_false_multivariate_identity(void)
{
    const TrytrAlgebraResult result = trytr_algebra_check("x+y = x-y");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_NOT_IDENTITY);
    return true;
}

static bool flags_division_by_variable_as_unsupported(void)
{
    const TrytrAlgebraResult result = trytr_algebra_check("x/x = 1");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_UNSUPPORTED);
    TEST_ASSERT(strstr(result.message, "division by a variable") != NULL);
    return true;
}

static bool flags_transcendental_functions_as_unsupported(void)
{
    const TrytrAlgebraResult result = trytr_algebra_check("\\sin(x)^2 = 1");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_UNSUPPORTED);
    return true;
}

static bool flags_unknown_latex_commands_as_unsupported(void)
{
    const TrytrAlgebraResult result = trytr_algebra_check("\\sum x = x");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_UNSUPPORTED);
    return true;
}

static bool rejects_malformed_equation(void)
{
    const TrytrAlgebraResult result = trytr_algebra_check("x + = y");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_INVALID);
    return true;
}

static bool rejects_null_input(void)
{
    const TrytrAlgebraResult result = trytr_algebra_check(NULL);
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_INVALID);
    return true;
}

static bool flags_degree_cap_as_unsupported(void)
{
    const TrytrAlgebraResult result =
        trytr_algebra_check("x^{12}*x = x^{13}");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_UNSUPPORTED);
    TEST_ASSERT(strstr(result.message, "degree exceeds 12") != NULL);
    return true;
}

static bool flags_variable_cap_as_unsupported(void)
{
    const TrytrAlgebraResult result = trytr_algebra_check("a+b+c+d+e+f+g+h+i = 0");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_UNSUPPORTED);
    TEST_ASSERT(strstr(result.message, "eight variables") != NULL);
    return true;
}

static bool rejects_division_by_zero(void)
{
    const TrytrAlgebraResult result = trytr_algebra_check("\\frac{x}{0}=0");
    TEST_ASSERT(result.status == TRYTR_ALGEBRA_INVALID);
    TEST_ASSERT(strstr(result.message, "division by zero") != NULL);
    return true;
}

typedef bool (*TestFunction)(void);

typedef struct TestCase {
    const char *name;
    TestFunction function;
} TestCase;

int main(void)
{
    static const TestCase tests[] = {
        {"accepts binomial expansion", accepts_binomial_expansion},
        {"accepts multivariate reordering", accepts_multivariate_reordering},
        {"accepts exact rational fractions", accepts_exact_rational_fractions},
        {"accepts exact decimal coefficients", accepts_exact_decimal_coefficients},
        {"accepts rational cancellation", accepts_rational_cancellation},
        {"accepts LaTeX variables and subscripts", accepts_latex_variables_and_subscripts},
        {"accepts wrapped display equation", accepts_wrapped_display_equation},
        {"accepts square and brace grouping", accepts_square_and_brace_grouping},
        {"rejects non-identity", rejects_non_identity},
        {"rejects false multivariate identity", rejects_false_multivariate_identity},
        {"flags variable division as unsupported", flags_division_by_variable_as_unsupported},
        {"flags transcendental functions as unsupported", flags_transcendental_functions_as_unsupported},
        {"flags unknown LaTeX commands as unsupported", flags_unknown_latex_commands_as_unsupported},
        {"rejects malformed equation", rejects_malformed_equation},
        {"rejects null input", rejects_null_input},
        {"flags degree cap", flags_degree_cap_as_unsupported},
        {"flags variable cap", flags_variable_cap_as_unsupported},
        {"rejects division by zero", rejects_division_by_zero},
    };
    const size_t count = sizeof(tests) / sizeof(tests[0]);
    for (size_t index = 0U; index < count; ++index) {
        if (!tests[index].function()) {
            fprintf(stderr, "[FAIL] %s\n", tests[index].name);
            return 1;
        }
    }
    printf("%zu algebra checker tests passed\n", count);
    return 0;
}
