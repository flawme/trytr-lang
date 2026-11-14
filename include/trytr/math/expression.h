#ifndef TRYTR_MATH_EXPRESSION_H
#define TRYTR_MATH_EXPRESSION_H

#include "trytr/math/real.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct TrytrMathExpression TrytrMathExpression;

typedef enum TrytrMathExpressionStatus {
    TRYTR_MATH_EXPRESSION_OK = 0,
    TRYTR_MATH_EXPRESSION_INVALID_ARGUMENT,
    TRYTR_MATH_EXPRESSION_SYNTAX,
    TRYTR_MATH_EXPRESSION_UNKNOWN_NAME,
    TRYTR_MATH_EXPRESSION_FUNCTION_ARITY,
    TRYTR_MATH_EXPRESSION_LIMIT_EXCEEDED,
    TRYTR_MATH_EXPRESSION_NO_MEMORY
} TrytrMathExpressionStatus;

/* Compile a bounded scalar expression containing x, pi, e, arithmetic, and
 * the pure functions sqrt, abs, sin, cos, tan, asin, acos, atan, exp, log,
 * log10, sinh, cosh, tanh, pow, atan2, hypot, min, and max. Trigonometric
 * functions use radians. The source need not be NUL-terminated. */
TrytrMathExpressionStatus trytr_math_expression_compile(
    const char *source,
    size_t length,
    TrytrMathExpression **out_expression,
    size_t *out_error_offset
);

/* Evaluates the compiled expression at x. Invalid domains and non-finite
 * results return false and reset out_value to zero. */
bool trytr_math_expression_evaluate(
    const TrytrMathExpression *expression,
    TrytrReal x,
    TrytrReal *out_value
);

const char *trytr_math_expression_status_string(
    TrytrMathExpressionStatus status
);
void trytr_math_expression_destroy(TrytrMathExpression *expression);

#endif
