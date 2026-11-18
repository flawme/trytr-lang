#include "algebra_internal.h"

#include <string.h>

AlgebraExpression algebra_expression_polynomial(Polynomial value)
{
    return (AlgebraExpression){.numerator = value,
        .denominator = polynomial_constant((Rational){1,1})};
}

static bool is_constant(const Polynomial *polynomial)
{
    return polynomial->count == 0u || (polynomial->count == 1u &&
        memcmp(polynomial->terms[0].monomial.powers,
               (uint8_t[ALGEBRA_MAX_VARIABLES]){0}, ALGEBRA_MAX_VARIABLES) == 0);
}

static bool normalize(Parser *parser, AlgebraExpression *value)
{
    if (!is_constant(&value->denominator)) return true;
    Rational reciprocal;
    Polynomial numerator;
    if (value->denominator.count == 0u ||
        !rational_divide((Rational){1,1}, value->denominator.terms[0].coefficient, &reciprocal) ||
        !polynomial_scale(parser, &value->numerator, reciprocal, &numerator)) {
        set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED, "rational expression exceeds the supported range");
        return false;
    }
    *value = algebra_expression_polynomial(numerator);
    return true;
}

bool algebra_expression_binary(Parser *parser, const AlgebraExpression *left,
    const AlgebraExpression *right, char operation, size_t offset, size_t length,
    AlgebraExpression *result)
{
    if (!algebra_charge(parser)) return false;
    if (operation == '/') {
        if (right->numerator.count == 0u) {
            set_error(parser, TRYTR_ALGEBRA_INVALID, "division by zero");
            return false;
        }
        if (!is_constant(&right->numerator)) {
            if (!parser->rational_functions) {
                set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                    "division by a variable expression is outside the polynomial checker");
                return false;
            }
            if (parser->condition_count == TRYTR_ALGEBRA_MAX_CONDITIONS) {
                set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED, "expression exceeds the 32-condition limit");
                return false;
            }
            parser->conditions[parser->condition_count++] = (TrytrAlgebraCondition){offset,length};
        }
        if (!polynomial_multiply(parser, &left->numerator, &right->denominator, &result->numerator) ||
            !polynomial_multiply(parser, &left->denominator, &right->numerator, &result->denominator)) return false;
    } else if (operation == '*') {
        if (!polynomial_multiply(parser, &left->numerator, &right->numerator, &result->numerator) ||
            !polynomial_multiply(parser, &left->denominator, &right->denominator, &result->denominator)) return false;
    } else {
        Polynomial first, second;
        if (polynomial_equal(&left->denominator, &right->denominator)) {
            if (!polynomial_add(parser, &left->numerator, &right->numerator,
                                operation == '-', &result->numerator)) return false;
            result->denominator = left->denominator;
        } else if (!polynomial_multiply(parser, &left->numerator, &right->denominator, &first) ||
            !polynomial_multiply(parser, &right->numerator, &left->denominator, &second) ||
            !polynomial_add(parser, &first, &second, operation == '-', &result->numerator) ||
            !polynomial_multiply(parser, &left->denominator, &right->denominator, &result->denominator)) return false;
    }
    return normalize(parser, result);
}

bool algebra_expression_power(Parser *parser, const AlgebraExpression *base,
    uint64_t exponent, AlgebraExpression *result)
{
    return polynomial_power(parser, &base->numerator, exponent, &result->numerator) &&
           polynomial_power(parser, &base->denominator, exponent, &result->denominator) &&
           normalize(parser, result);
}
