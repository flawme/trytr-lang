#include "algebra_internal.h"

#include <stdio.h>
#include <string.h>

static bool consume_math_delimiter(Parser *parser, bool opening, char *closing)
{
    if (opening && consume(parser, "\\(")) {
        *closing = ')';
        return true;
    }
    if (opening && consume(parser, "\\[")) {
        *closing = ']';
        return true;
    }
    if (opening && consume(parser, "$$")) {
        *closing = 'D';
        return true;
    }
    if (opening && consume(parser, "$")) {
        *closing = '$';
        return true;
    }
    if (!opening && *closing == ')' && consume(parser, "\\)")) {
        return true;
    }
    if (!opening && *closing == ']' && consume(parser, "\\]")) {
        return true;
    }
    if (!opening && *closing == 'D' && consume(parser, "$$")) {
        return true;
    }
    if (!opening && *closing == '$' && consume(parser, "$")) {
        return true;
    }
    return false;
}

static TrytrAlgebraResult check(const char *source, bool rational_functions)
{
    Parser parser = {0};
    parser.error_status = TRYTR_ALGEBRA_INVALID;
    parser.rational_functions = rational_functions;
    if (source == NULL) {
        return (TrytrAlgebraResult){.status = TRYTR_ALGEBRA_INVALID,
                                    .offset = 0U,
                                    .message = "equation source is null"};
    }
    parser.source = source;
    parser.length = strnlen(source, ALGEBRA_MAX_SOURCE + 1U);
    if (parser.length > ALGEBRA_MAX_SOURCE) {
        return (TrytrAlgebraResult){.status = TRYTR_ALGEBRA_INVALID,
                                    .offset = 0U,
                                    .message = "equation source exceeds 64 KiB"};
    }
    char closing = '\0';
    (void)consume_math_delimiter(&parser, true, &closing);
    AlgebraExpression left;
    AlgebraExpression right;
    bool parsed = algebra_parse_expression(&parser, 0U, &left);
    if (parsed && !consume(&parser, "=")) {
        set_error(&parser, TRYTR_ALGEBRA_INVALID,
                  "expected '=' between the two expressions");
        parsed = false;
    }
    if (parsed) {
        parsed = algebra_parse_expression(&parser, 0U, &right);
    }
    if (parsed && closing != '\0' && !consume_math_delimiter(&parser, false, &closing)) {
        set_error(&parser, TRYTR_ALGEBRA_INVALID,
                  "missing closing math delimiter");
        parsed = false;
    }
    skip_space(&parser);
    if (parsed && parser.cursor != parser.length) {
        set_error(&parser, TRYTR_ALGEBRA_INVALID,
                  "unexpected content after the equality");
        parsed = false;
    }
    Polynomial first, second;
    if (parsed && (!polynomial_multiply(&parser, &left.numerator, &right.denominator, &first) ||
                   !polynomial_multiply(&parser, &right.numerator, &left.denominator, &second))) parsed = false;
    if (parsed && polynomial_equal(&first, &second)) {
        TrytrAlgebraResult result = {.status = parser.condition_count == 0u
            ? TRYTR_ALGEBRA_VERIFIED : TRYTR_ALGEBRA_CONDITIONAL, .offset = parser.cursor,
            .condition_count = parser.condition_count};
        (void)memcpy(result.conditions, parser.conditions,
                     parser.condition_count * sizeof(parser.conditions[0]));
        (void)snprintf(result.message, sizeof(result.message), "%s", parser.condition_count == 0u
            ? "identity verified over rational polynomials"
            : "rational identity holds where every listed divisor is defined and nonzero");
        return result;
    }
    if (parsed) {
        return (TrytrAlgebraResult){.status = TRYTR_ALGEBRA_NOT_IDENTITY,
                                    .offset = parser.cursor,
                                    .message = "equality is not an algebraic identity"};
    }
    TrytrAlgebraResult result = {.status = parser.error_status,
                                 .offset = parser.cursor,
                                 .message = {0}};
    (void)snprintf(result.message, sizeof(result.message), "%s",
                   parser.error[0] == '\0' ? "invalid polynomial equality"
                                             : parser.error);
    return result;
}

TrytrAlgebraResult trytr_algebra_check(const char *source)
{ return check(source, false); }

TrytrAlgebraResult trytr_algebra_check_rational(const char *source)
{ return check(source, true); }
