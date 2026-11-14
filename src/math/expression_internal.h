#ifndef TRYTR_SRC_MATH_EXPRESSION_INTERNAL_H
#define TRYTR_SRC_MATH_EXPRESSION_INTERNAL_H

#include "trytr/math/expression.h"

#include <stddef.h>

#define EXPRESSION_MAX_INSTRUCTIONS 256u

typedef enum ExpressionOpcode {
    EXPRESSION_PUSH_CONSTANT = 0,
    EXPRESSION_PUSH_X,
    EXPRESSION_NEGATE,
    EXPRESSION_ADD,
    EXPRESSION_SUBTRACT,
    EXPRESSION_MULTIPLY,
    EXPRESSION_DIVIDE,
    EXPRESSION_POWER,
    EXPRESSION_SQRT,
    EXPRESSION_ABS,
    EXPRESSION_SIN,
    EXPRESSION_COS,
    EXPRESSION_TAN,
    EXPRESSION_ASIN,
    EXPRESSION_ACOS,
    EXPRESSION_ATAN,
    EXPRESSION_EXP,
    EXPRESSION_LOG,
    EXPRESSION_LOG10,
    EXPRESSION_SINH,
    EXPRESSION_COSH,
    EXPRESSION_TANH,
    EXPRESSION_ATAN2,
    EXPRESSION_HYPOT,
    EXPRESSION_MIN,
    EXPRESSION_MAX
} ExpressionOpcode;

typedef struct ExpressionInstruction {
    ExpressionOpcode opcode;
    double constant;
} ExpressionInstruction;

struct TrytrMathExpression {
    size_t instruction_count;
    ExpressionInstruction instructions[];
};

#endif
