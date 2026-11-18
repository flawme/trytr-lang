#include "algebra_internal.h"

#include <ctype.h>
#include <string.h>

static bool parse_group(Parser *parser, size_t depth, AlgebraExpression *result)
{
    const char *closing = NULL;
    if (consume(parser, "\\left(")) closing = "\\right)";
    else if (consume(parser, "\\left[")) closing = "\\right]";
    else if (consume(parser, "(")) closing = ")";
    else if (consume(parser, "[")) closing = "]";
    else if (consume(parser, "{")) closing = "}";
    if (closing == NULL) return false;
    if (!algebra_parse_expression(parser, depth + 1u, result)) return false;
    if (!consume(parser, closing)) {
        set_error(parser, TRYTR_ALGEBRA_INVALID, "expected closing parenthesis in expression");
        return false;
    }
    return true;
}

static bool parse_fraction(Parser *parser, size_t depth, AlgebraExpression *result)
{
    if (!(consume(parser, "\\frac") || consume(parser, "\\dfrac"))) return false;
    AlgebraExpression numerator, denominator;
    if (!consume(parser, "{") || !algebra_parse_expression(parser, depth + 1u, &numerator) ||
        !consume(parser, "}") || !consume(parser, "{")) {
        set_error(parser, TRYTR_ALGEBRA_INVALID, "malformed numerator in \\frac");
        return false;
    }
    const size_t start = parser->cursor;
    if (!algebra_parse_expression(parser, depth + 1u, &denominator)) return false;
    const size_t length = parser->cursor - start;
    if (!consume(parser, "}")) {
        set_error(parser, TRYTR_ALGEBRA_INVALID, "malformed denominator in \\frac");
        return false;
    }
    return algebra_expression_binary(parser, &numerator, &denominator, '/', start, length, result);
}

static bool parse_primary(Parser *parser, size_t depth, AlgebraExpression *result)
{
    if (depth > ALGEBRA_MAX_NESTING) {
        set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED, "expression exceeds the nesting limit");
        return false;
    }
    if (parse_fraction(parser, depth, result)) return true;
    if (parser->error[0] != '\0') return false;
    if (parse_group(parser, depth, result)) return true;
    if (parser->error[0] != '\0') return false;
    skip_space(parser);
    Polynomial polynomial;
    if (parser->cursor < parser->length && isdigit((unsigned char)parser->source[parser->cursor]) != 0) {
        Rational number;
        if (!algebra_parse_number(parser, &number)) return false;
        polynomial = polynomial_constant(number);
    } else if (!algebra_parse_variable(parser, &polynomial)) return false;
    *result = algebra_expression_polynomial(polynomial);
    return true;
}

static bool parse_power(Parser *parser, size_t depth, AlgebraExpression *result)
{
    const size_t start = parser->cursor;
    if (!parse_primary(parser, depth, result)) return false;
    const size_t base_length = parser->cursor - start;
    if (!consume(parser, "^")) return true;
    const bool braced = consume(parser, "{");
    const bool negative = consume(parser, "-");
    if (negative && !parser->rational_functions) {
        set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED, "negative powers require rational-function checking");
        return false;
    }
    uint64_t exponent = 0u;
    if (!algebra_parse_unsigned(parser, ALGEBRA_MAX_DEGREE, &exponent) ||
        (braced && !consume(parser, "}"))) {
        set_error(parser, TRYTR_ALGEBRA_INVALID, "expected a bounded integer exponent");
        return false;
    }
    AlgebraExpression base = *result;
    if (negative && exponent != 0u) {
        const AlgebraExpression one = algebra_expression_polynomial(polynomial_constant((Rational){1,1}));
        if (!algebra_expression_binary(parser, &one, &base, '/', start, base_length, result)) return false;
        base = *result;
    }
    return algebra_expression_power(parser, &base, exponent, result);
}

static bool parse_unary(Parser *parser, size_t depth, AlgebraExpression *result)
{
    if (depth > ALGEBRA_MAX_NESTING) {
        set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED, "expression exceeds the nesting limit");
        return false;
    }
    if (consume(parser, "+")) return parse_unary(parser, depth + 1u, result);
    if (consume(parser, "-")) {
        if (!parse_unary(parser, depth + 1u, result)) return false;
        for (size_t i = 0u; i < result->numerator.count; ++i) {
            /* Rational normalization excludes INT64_MIN. */
            result->numerator.terms[i].coefficient.numerator *= -1;
        }
        return true;
    }
    return parse_power(parser, depth, result);
}

static bool starts_primary(Parser *parser)
{
    skip_space(parser);
    if (parser->cursor == parser->length) return false;
    const unsigned char c = (unsigned char)parser->source[parser->cursor];
    if (isalnum(c) != 0 || c == '(' || c == '[' || c == '{') return true;
    if (c != '\\' || parser->cursor + 1u >= parser->length ||
        isalpha((unsigned char)parser->source[parser->cursor + 1u]) == 0) return false;
    return parser->length - parser->cursor < 6u || memcmp(parser->source + parser->cursor, "\\right", 6u) != 0;
}

static bool parse_term(Parser *parser, size_t depth, AlgebraExpression *result)
{
    if (!parse_unary(parser, depth, result)) return false;
    for (;;) {
        const bool multiply = consume(parser, "*") || consume(parser, "\\cdot") || consume(parser, "\\times");
        const bool divide = !multiply && consume(parser, "/");
        if (!multiply && !divide && !starts_primary(parser)) return true;
        const size_t start = parser->cursor;
        AlgebraExpression right, combined;
        if (!parse_unary(parser, depth, &right) || !algebra_expression_binary(parser, result, &right,
            divide ? '/' : '*', start, parser->cursor - start, &combined)) return false;
        *result = combined;
    }
}

bool algebra_parse_expression(Parser *parser, size_t depth, AlgebraExpression *result)
{
    if (!parse_term(parser, depth, result)) return false;
    for (;;) {
        const bool subtract = consume(parser, "-");
        const bool add = !subtract && consume(parser, "+");
        if (!subtract && !add) return true;
        AlgebraExpression right, combined;
        if (!parse_term(parser, depth, &right) ||
            !algebra_expression_binary(parser, result, &right, subtract ? '-' : '+', 0u, 0u, &combined)) return false;
        *result = combined;
    }
}
