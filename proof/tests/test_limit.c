#include "trytr/limit.h"

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

static bool accepts_quadratic_limit(void)
{
    const TrytrProofResult result = trytr_limit_check(
        "\\begin{limitproof}\n"
        "\\lim_{x\\to 2}(x^2+1)=5\n"
        "\\delta=\\min(1,\\frac{\\varepsilon}{5})\n"
        "\\end{limitproof}\n");
    TEST_ASSERT(result.valid);
    TEST_ASSERT(strstr(result.message, "required bound 5") != NULL);
    return true;
}

static bool accepts_latex_exponent_implicit_products_and_epsilon_alias(void)
{
    const TrytrProofResult result = trytr_limit_check(
        "\\lim_{ x \\to 1 }(x^{3}-2x+1)=0\n"
        "\\delta=\\min\\left(1,\\frac{\\epsilon}{7}\\right)");
    TEST_ASSERT(result.valid);
    TEST_ASSERT(strstr(result.message, "required bound 7") != NULL);
    return true;
}

static bool accepts_conservative_bound(void)
{
    const TrytrProofResult result = trytr_limit_check(
        "\\lim_{x\\to 2}(x^2+1)=5\n"
        "\\delta=\\min(1,\\frac{\\varepsilon}{9})");
    TEST_ASSERT(result.valid);
    TEST_ASSERT(strstr(result.message, "required bound 5") != NULL);
    return true;
}

static bool accepts_constant_polynomial(void)
{
    const TrytrProofResult result = trytr_limit_check(
        "\\lim_{x\\to -4}(7)=7\n"
        "\\delta=\\min(1,\\frac{\\varepsilon}{1})");
    TEST_ASSERT(result.valid);
    TEST_ASSERT(strstr(result.message, "required bound 0") != NULL);
    return true;
}

static bool accepts_parenthesized_power_at_negative_point(void)
{
    const TrytrProofResult result = trytr_limit_check(
        "\\lim_{x\\to -2}\\left((x+2)^2\\right)=0\n"
        "\\delta=\\min(1,\\frac{\\varepsilon}{5})");
    TEST_ASSERT(result.valid);
    TEST_ASSERT(strstr(result.message, "required bound 5") != NULL);
    return true;
}

static bool rejects_incorrect_limit_value(void)
{
    const TrytrProofResult result = trytr_limit_check(
        "\\lim_{x\\to 2}(x^2+1)=6\n"
        "\\delta=\\min(1,\\frac{\\varepsilon}{5})");
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(strstr(result.message, "stated limit") != NULL);
    return true;
}

static bool rejects_undersized_bound(void)
{
    const TrytrProofResult result = trytr_limit_check(
        "\\lim_{x\\to 2}(x^2+1)=5\n"
        "\\delta=\\min(1,\\frac{\\varepsilon}{4})");
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(strstr(result.message, "smaller") != NULL);
    return true;
}

static bool rejects_zero_denominator(void)
{
    const TrytrProofResult result = trytr_limit_check(
        "\\lim_{x\\to 2}(x^2+1)=5\n"
        "\\delta=\\min(1,\\frac{\\varepsilon}{0})");
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(strstr(result.message, "positive") != NULL);
    return true;
}

static bool rejects_missing_environment_end(void)
{
    const TrytrProofResult result = trytr_limit_check(
        "\\begin{limitproof}\n"
        "\\lim_{x\\to 2}(x^2+1)=5\n"
        "\\delta=\\min(1,\\frac{\\varepsilon}{5})\n");
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(strstr(result.message, "missing") != NULL);
    return true;
}

static bool rejects_excessive_degree(void)
{
    const TrytrProofResult result = trytr_limit_check(
        "\\lim_{x\\to 1}(x^{17})=1\n"
        "\\delta=\\min(1,\\frac{\\varepsilon}{1})");
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(strstr(result.message, "supported limit") != NULL);
    return true;
}

static bool rejects_degree_overflow_from_product(void)
{
    const TrytrProofResult result = trytr_limit_check(
        "\\lim_{x\\to 0}(x^{16}*x^2)=0\n"
        "\\delta=\\min(1,\\frac{\\varepsilon}{1})");
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(strstr(result.message, "degree exceeds 16") != NULL);
    return true;
}

static bool rejects_trailing_claims(void)
{
    const TrytrProofResult result = trytr_limit_check(
        "\\lim_{x\\to 2}(x^2+1)=5\n"
        "\\delta=\\min(1,\\frac{\\varepsilon}{5})\n"
        "\\delta=100");
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(strstr(result.message, "unexpected content") != NULL);
    return true;
}

static bool rejects_null_source(void)
{
    const TrytrProofResult result = trytr_limit_check(NULL);
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(strstr(result.message, "null") != NULL);
    return true;
}

static bool rejects_source_over_limit(void)
{
    static char source[65538];
    (void)memset(source, ' ', sizeof(source) - 1U);
    source[sizeof(source) - 1U] = '\0';
    const TrytrProofResult result = trytr_limit_check(source);
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(strstr(result.message, "64 KiB") != NULL);
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
        {"accepts quadratic epsilon-delta certificate", accepts_quadratic_limit},
        {"accepts LaTeX exponent and implicit product", accepts_latex_exponent_implicit_products_and_epsilon_alias},
        {"accepts conservative denominator bound", accepts_conservative_bound},
        {"accepts constant polynomial", accepts_constant_polynomial},
        {"accepts parenthesized power at negative point", accepts_parenthesized_power_at_negative_point},
        {"rejects incorrect limit value", rejects_incorrect_limit_value},
        {"rejects undersized polynomial bound", rejects_undersized_bound},
        {"rejects zero delta denominator", rejects_zero_denominator},
        {"rejects missing environment end", rejects_missing_environment_end},
        {"rejects unsupported degree", rejects_excessive_degree},
        {"rejects degree overflow from product", rejects_degree_overflow_from_product},
        {"rejects trailing unverified claims", rejects_trailing_claims},
        {"rejects null source", rejects_null_source},
        {"rejects source over input cap", rejects_source_over_limit},
    };
    const size_t count = sizeof(tests) / sizeof(tests[0]);
    for (size_t index = 0U; index < count; ++index) {
        if (!tests[index].function()) {
            fprintf(stderr, "[FAIL] %s\n", tests[index].name);
            return 1;
        }
    }
    printf("%zu limit checker tests passed\n", count);
    return 0;
}
