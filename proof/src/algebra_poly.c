#include "algebra_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void set_error(Parser *parser, TrytrAlgebraStatus status, const char *message)
{
    if (parser->error[0] == '\0') {
        parser->error_status = status;
        (void)snprintf(parser->error, sizeof(parser->error), "%s", message);
    }
}

void skip_space(Parser *parser)
{
    while (parser->cursor < parser->length &&
           isspace((unsigned char)parser->source[parser->cursor]) != 0) {
        ++parser->cursor;
    }
}

bool consume(Parser *parser, const char *token)
{
    skip_space(parser);
    const size_t token_length = strlen(token);
    if (token_length > parser->length - parser->cursor ||
        memcmp(parser->source + parser->cursor, token, token_length) != 0) {
        return false;
    }
    parser->cursor += token_length;
    return true;
}

static uint64_t magnitude(int64_t value)
{
    return value < 0 ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
}

static uint64_t gcd_u64(uint64_t left, uint64_t right)
{
    while (right != 0U) {
        const uint64_t remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

static bool checked_add_i64(int64_t left, int64_t right, int64_t *result)
{
    if ((right > 0 && left > INT64_MAX - right) ||
        (right < 0 && left < INT64_MIN - right)) {
        return false;
    }
    *result = left + right;
    return true;
}

static bool checked_multiply_i64(int64_t left, int64_t right, int64_t *result)
{
    if (left == 0 || right == 0) {
        *result = 0;
        return true;
    }
    if ((left == -1 && right == INT64_MIN) ||
        (right == -1 && left == INT64_MIN)) {
        return false;
    }
    if (left > 0) {
        if ((right > 0 && left > INT64_MAX / right) ||
            (right < 0 && right < INT64_MIN / left)) {
            return false;
        }
    } else if ((right > 0 && left < INT64_MIN / right) ||
               (right < 0 && left < INT64_MAX / right)) {
        return false;
    }
    *result = left * right;
    return true;
}

bool rational_normalize(Rational *value)
{
    if (value->denominator == 0 || value->numerator == INT64_MIN ||
        value->denominator == INT64_MIN) {
        return false;
    }
    if (value->denominator < 0) {
        value->denominator = -value->denominator;
        value->numerator = -value->numerator;
    }
    if (value->numerator == 0) {
        value->denominator = 1;
        return true;
    }
    const uint64_t divisor = gcd_u64(magnitude(value->numerator),
                                     (uint64_t)value->denominator);
    value->numerator /= (int64_t)divisor;
    value->denominator /= (int64_t)divisor;
    return true;
}

bool rational_add(Rational left, Rational right, Rational *result)
{
    const uint64_t divisor = gcd_u64((uint64_t)left.denominator,
                                     (uint64_t)right.denominator);
    const int64_t left_scale = right.denominator / (int64_t)divisor;
    const int64_t right_scale = left.denominator / (int64_t)divisor;
    int64_t left_numerator = 0;
    int64_t right_numerator = 0;
    int64_t numerator = 0;
    int64_t denominator = 0;
    if (!checked_multiply_i64(left.numerator, left_scale, &left_numerator) ||
        !checked_multiply_i64(right.numerator, right_scale, &right_numerator) ||
        !checked_add_i64(left_numerator, right_numerator, &numerator) ||
        !checked_multiply_i64(left.denominator, left_scale, &denominator)) {
        return false;
    }
    *result = (Rational){.numerator = numerator, .denominator = denominator};
    return rational_normalize(result);
}

bool rational_multiply(Rational left, Rational right, Rational *result)
{
    const uint64_t first_gcd = gcd_u64(magnitude(left.numerator),
                                       (uint64_t)right.denominator);
    const uint64_t second_gcd = gcd_u64(magnitude(right.numerator),
                                        (uint64_t)left.denominator);
    const int64_t left_numerator = left.numerator / (int64_t)first_gcd;
    const int64_t right_denominator = right.denominator / (int64_t)first_gcd;
    const int64_t right_numerator = right.numerator / (int64_t)second_gcd;
    const int64_t left_denominator = left.denominator / (int64_t)second_gcd;
    int64_t numerator = 0;
    int64_t denominator = 0;
    if (!checked_multiply_i64(left_numerator, right_numerator, &numerator) ||
        !checked_multiply_i64(left_denominator, right_denominator, &denominator)) {
        return false;
    }
    *result = (Rational){.numerator = numerator, .denominator = denominator};
    return rational_normalize(result);
}

bool rational_divide(Rational left, Rational right, Rational *result)
{
    if (right.numerator == 0 || right.numerator == INT64_MIN) {
        return false;
    }
    const int64_t reciprocal_numerator = right.numerator < 0
                                             ? -right.denominator
                                             : right.denominator;
    const int64_t reciprocal_denominator = (int64_t)magnitude(right.numerator);
    return rational_multiply(left,
                             (Rational){.numerator = reciprocal_numerator,
                                        .denominator = reciprocal_denominator},
                             result);
}

bool rational_equal(Rational left, Rational right)
{
    return left.numerator == right.numerator && left.denominator == right.denominator;
}

Polynomial polynomial_zero(void)
{
    return (Polynomial){0};
}

bool monomial_equal(const Monomial *left, const Monomial *right)
{
    return memcmp(left->powers, right->powers, sizeof(left->powers)) == 0;
}

bool polynomial_add_term(Parser *parser, Polynomial *polynomial,
                                const Monomial *monomial, Rational coefficient)
{
    if (coefficient.numerator == 0) {
        return true;
    }
    for (size_t index = 0U; index < polynomial->count; ++index) {
        if (monomial_equal(&polynomial->terms[index].monomial, monomial)) {
            Rational sum;
            if (!rational_add(polynomial->terms[index].coefficient,
                              coefficient, &sum)) {
                set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                          "rational coefficient arithmetic exceeds the supported range");
                return false;
            }
            if (sum.numerator == 0) {
                --polynomial->count;
                polynomial->terms[index] = polynomial->terms[polynomial->count];
            } else {
                polynomial->terms[index].coefficient = sum;
            }
            return true;
        }
    }
    if (polynomial->count >= ALGEBRA_MAX_TERMS) {
        set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                  "polynomial exceeds the 128-term limit");
        return false;
    }
    polynomial->terms[polynomial->count++] =
        (AlgebraTerm){.monomial = *monomial, .coefficient = coefficient};
    return true;
}

Polynomial polynomial_constant(Rational value)
{
    Polynomial result = polynomial_zero();
    if (value.numerator != 0) {
        result.terms[0] = (AlgebraTerm){.monomial = {.powers = {0}},
                                        .coefficient = value};
        result.count = 1U;
    }
    return result;
}

bool polynomial_add(Parser *parser, const Polynomial *left,
                           const Polynomial *right, bool subtract,
                           Polynomial *result)
{
    *result = *left;
    for (size_t index = 0U; index < right->count; ++index) {
        Rational coefficient = right->terms[index].coefficient;
        if (subtract) {
            if (coefficient.numerator == INT64_MIN) {
                set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                          "rational coefficient exceeds the supported range");
                return false;
            }
            coefficient.numerator = -coefficient.numerator;
        }
        if (!polynomial_add_term(parser, result, &right->terms[index].monomial,
                                 coefficient)) {
            return false;
        }
    }
    return true;
}
bool polynomial_multiply(Parser *parser, const Polynomial *left,
                                const Polynomial *right, Polynomial *result)
{
    *result = polynomial_zero();
    for (size_t left_index = 0U; left_index < left->count; ++left_index) {
        for (size_t right_index = 0U; right_index < right->count; ++right_index) {
            Monomial monomial = {0};
            unsigned total_degree = 0U;
            for (size_t variable = 0U; variable < ALGEBRA_MAX_VARIABLES; ++variable) {
                const unsigned power =
                    (unsigned)left->terms[left_index].monomial.powers[variable] +
                    (unsigned)right->terms[right_index].monomial.powers[variable];
                total_degree += power;
                if (power > ALGEBRA_MAX_DEGREE) {
                    set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                              "polynomial degree exceeds 12");
                    return false;
                }
                monomial.powers[variable] = (uint8_t)power;
            }
            if (total_degree > ALGEBRA_MAX_DEGREE) {
                set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                          "polynomial degree exceeds 12");
                return false;
            }
            Rational coefficient;
            if (!rational_multiply(left->terms[left_index].coefficient,
                                   right->terms[right_index].coefficient,
                                   &coefficient) ||
                !polynomial_add_term(parser, result, &monomial, coefficient)) {
                if (parser->error[0] == '\0') {
                    set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                              "rational coefficient arithmetic exceeds the supported range");
                }
                return false;
            }
        }
    }
    return true;
}

bool polynomial_scale(Parser *parser, const Polynomial *polynomial,
                             Rational scalar, Polynomial *result)
{
    *result = polynomial_zero();
    for (size_t index = 0U; index < polynomial->count; ++index) {
        Rational coefficient;
        if (!rational_multiply(polynomial->terms[index].coefficient, scalar,
                               &coefficient) ||
            !polynomial_add_term(parser, result, &polynomial->terms[index].monomial,
                                 coefficient)) {
            if (parser->error[0] == '\0') {
                set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                          "rational coefficient arithmetic exceeds the supported range");
            }
            return false;
        }
    }
    return true;
}

bool polynomial_power(Parser *parser, const Polynomial *base,
                             uint64_t exponent, Polynomial *result)
{
    *result = polynomial_constant((Rational){.numerator = 1, .denominator = 1});
    for (uint64_t index = 0U; index < exponent; ++index) {
        Polynomial product;
        if (!polynomial_multiply(parser, result, base, &product)) {
            return false;
        }
        *result = product;
    }
    return true;
}
