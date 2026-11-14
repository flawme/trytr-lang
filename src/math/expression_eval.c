#include "expression_internal.h"

#include <math.h>

static bool apply_unary(ExpressionOpcode opcode, double value, double *out)
{
    switch (opcode) {
        case EXPRESSION_NEGATE: *out = -value; break;
        case EXPRESSION_SQRT: *out = trytr_real_sqrt(value); break;
        case EXPRESSION_ABS: *out = fabs(value); break;
        case EXPRESSION_SIN: *out = trytr_real_sin(value); break;
        case EXPRESSION_COS: *out = trytr_real_cos(value); break;
        case EXPRESSION_TAN: *out = trytr_real_tan(value); break;
        case EXPRESSION_ASIN: *out = trytr_real_asin(value); break;
        case EXPRESSION_ACOS: *out = trytr_real_acos(value); break;
        case EXPRESSION_ATAN: *out = trytr_real_atan(value); break;
        case EXPRESSION_EXP: *out = trytr_real_exp(value); break;
        case EXPRESSION_LOG: *out = trytr_real_log(value); break;
        case EXPRESSION_LOG10: *out = trytr_real_log10(value); break;
        case EXPRESSION_SINH: *out = trytr_real_sinh(value); break;
        case EXPRESSION_COSH: *out = trytr_real_cosh(value); break;
        case EXPRESSION_TANH: *out = trytr_real_tanh(value); break;
        default: return false;
    }
    return isfinite(*out);
}

static bool apply_binary(ExpressionOpcode opcode, double left, double right,
                         double *out)
{
    switch (opcode) {
        case EXPRESSION_ADD: *out = left + right; break;
        case EXPRESSION_SUBTRACT: *out = left - right; break;
        case EXPRESSION_MULTIPLY: *out = left * right; break;
        case EXPRESSION_DIVIDE:
            if (right == 0.0) return false;
            *out = left / right;
            break;
        case EXPRESSION_POWER: *out = trytr_real_pow(left, right); break;
        case EXPRESSION_ATAN2: *out = trytr_real_atan2(left, right); break;
        case EXPRESSION_HYPOT: *out = trytr_real_hypot(left, right); break;
        case EXPRESSION_MIN: *out = fmin(left, right); break;
        case EXPRESSION_MAX: *out = fmax(left, right); break;
        default: return false;
    }
    return isfinite(*out);
}

bool trytr_math_expression_evaluate(const TrytrMathExpression *expression,
                                   TrytrReal x, TrytrReal *out_value)
{
    if (out_value == NULL) return false;
    *out_value = 0.0;
    if (expression == NULL || !isfinite(x) ||
        expression->instruction_count == 0u ||
        expression->instruction_count > EXPRESSION_MAX_INSTRUCTIONS) return false;

    double stack[EXPRESSION_MAX_INSTRUCTIONS];
    size_t stack_size = 0u;
    for (size_t index = 0u; index < expression->instruction_count; ++index) {
        const ExpressionInstruction instruction = expression->instructions[index];
        if (instruction.opcode == EXPRESSION_PUSH_CONSTANT ||
            instruction.opcode == EXPRESSION_PUSH_X) {
            if (stack_size >= EXPRESSION_MAX_INSTRUCTIONS) return false;
            stack[stack_size++] = instruction.opcode == EXPRESSION_PUSH_X
                ? x : instruction.constant;
            continue;
        }
        double value = 0.0;
        if (instruction.opcode == EXPRESSION_NEGATE ||
            (instruction.opcode >= EXPRESSION_SQRT &&
             instruction.opcode <= EXPRESSION_TANH)) {
            if (stack_size < 1u ||
                !apply_unary(instruction.opcode, stack[stack_size - 1u], &value))
                return false;
            stack[stack_size - 1u] = value;
            continue;
        }
        if (stack_size < 2u) return false;
        const double right = stack[--stack_size];
        const double left = stack[stack_size - 1u];
        if (!apply_binary(instruction.opcode, left, right, &value)) return false;
        stack[stack_size - 1u] = value;
    }
    if (stack_size != 1u || !isfinite(stack[0])) return false;
    *out_value = stack[0];
    return true;
}
