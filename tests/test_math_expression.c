#include "test.h"

#include "trytr/math/expression.h"

#include <math.h>
#include <string.h>

static bool close_to(double actual, double expected)
{
    return fabs(actual - expected) <= 1e-12 * fmax(1.0, fabs(expected));
}

static bool evaluate(const char *source, double x, double expected)
{
    TrytrMathExpression *expression = NULL;
    size_t error_offset = 0u;
    if (trytr_math_expression_compile(source, strlen(source), &expression,
                                      &error_offset) != TRYTR_MATH_EXPRESSION_OK)
        return false;
    double result = NAN;
    const bool evaluated = trytr_math_expression_evaluate(expression, x, &result);
    trytr_math_expression_destroy(expression);
    return evaluated && close_to(result, expected);
}

static bool test_expression_precedence_and_math_functions(void)
{
    TEST_ASSERT(evaluate("sqrt(3^2 + 4^2) + sin(pi/2) + 2^3^2", 0.0, 518.0));
    TEST_ASSERT(evaluate("-2^2 + 2^-2", 0.0, -3.75));
    TEST_ASSERT(evaluate("hypot(x, 3) + max(2, abs(-4))", 4.0, 9.0));
    TEST_ASSERT(evaluate("atan2(1, 1) * 4 / pi", 0.0, 1.0));
    TEST_ASSERT(evaluate("cos(0) + tan(0) + asin(1) + acos(1) + atan(1)",
                         0.0, 1.0 + TRYTR_REAL_PI * 0.75));
    TEST_ASSERT(evaluate("exp(0) + log(e) + log10(100) + sinh(0) + cosh(0)",
                         0.0, 5.0));
    TEST_ASSERT(evaluate("tanh(1) + min(2, 3) + max(2, 3)", 0.0,
                         tanh(1.0) + 5.0));
    return true;
}

static bool compile_fails(const char *source,
                          TrytrMathExpressionStatus expected_status)
{
    TrytrMathExpression *expression = (TrytrMathExpression *)1;
    size_t error_offset = 0u;
    const TrytrMathExpressionStatus status = trytr_math_expression_compile(
        source, strlen(source), &expression, &error_offset);
    const bool valid = status == expected_status && expression == NULL &&
                       error_offset <= strlen(source);
    trytr_math_expression_destroy(expression);
    return valid;
}

static bool test_expression_rejects_unknown_names_bad_arity_and_syntax(void)
{
    TEST_ASSERT(compile_fails("unknown(x)", TRYTR_MATH_EXPRESSION_UNKNOWN_NAME));
    TEST_ASSERT(compile_fails("sin(x, 2)", TRYTR_MATH_EXPRESSION_FUNCTION_ARITY));
    TEST_ASSERT(compile_fails("x +", TRYTR_MATH_EXPRESSION_SYNTAX));
    TEST_ASSERT(compile_fails("1e999", TRYTR_MATH_EXPRESSION_SYNTAX));
    return true;
}

static bool test_expression_domain_failures_are_finite_and_reset_output(void)
{
    static const char *const invalid[] = {
        "sqrt(-1)", "log(0)", "1 / (x - 2)", "exp(1000)", "pow(-2, 0.5)"
    };
    for (size_t index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        TrytrMathExpression *expression = NULL;
        size_t error_offset = 0u;
        TEST_ASSERT(trytr_math_expression_compile(invalid[index],
            strlen(invalid[index]), &expression, &error_offset) == TRYTR_MATH_EXPRESSION_OK);
        double result = 123.0;
        TEST_ASSERT(!trytr_math_expression_evaluate(expression, 2.0, &result));
        TEST_ASSERT(result == 0.0);
        trytr_math_expression_destroy(expression);
    }
    return true;
}

static bool test_expression_limits_and_invalid_api_arguments(void)
{
    TrytrMathExpression *expression = NULL;
    size_t error_offset = 0u;
    TEST_ASSERT(trytr_math_expression_compile(NULL, 0u, &expression,
        &error_offset) == TRYTR_MATH_EXPRESSION_INVALID_ARGUMENT);
    TEST_ASSERT(trytr_math_expression_compile("x", 1u, NULL,
        &error_offset) == TRYTR_MATH_EXPRESSION_INVALID_ARGUMENT);
    TEST_ASSERT(trytr_math_expression_compile("x", 1u, &expression,
        NULL) == TRYTR_MATH_EXPRESSION_INVALID_ARGUMENT);
    char oversized[1024];
    size_t length = 0u;
    for (size_t term = 0u; term < 300u; ++term) {
        oversized[length++] = 'x';
        if (term + 1u < 300u) oversized[length++] = '+';
    }
    TrytrMathExpressionStatus status = trytr_math_expression_compile(
        oversized, length, &expression, &error_offset);
    TEST_ASSERT(status == TRYTR_MATH_EXPRESSION_LIMIT_EXCEEDED);
    TEST_ASSERT(expression == NULL);
    return true;
}

static const TrytrTestCase tests[] = {
    {"expression precedence and functions", test_expression_precedence_and_math_functions},
    {"expression syntax and function errors",
     test_expression_rejects_unknown_names_bad_arity_and_syntax},
    {"expression domain and overflow failures",
     test_expression_domain_failures_are_finite_and_reset_output},
    {"expression API arguments and limits",
     test_expression_limits_and_invalid_api_arguments}
};

TEST_MAIN(tests)
