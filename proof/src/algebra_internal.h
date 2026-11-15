#ifndef TRYTR_ALGEBRA_INTERNAL_H
#define TRYTR_ALGEBRA_INTERNAL_H

#include "trytr/algebra.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ALGEBRA_MAX_SOURCE 65536U
#define ALGEBRA_MAX_VARIABLES 8U
#define ALGEBRA_MAX_TERMS 128U
#define ALGEBRA_MAX_DEGREE 12U
#define ALGEBRA_MAX_NESTING 32U
#define ALGEBRA_MAX_NAME 31U
#define ALGEBRA_MAX_LITERAL INT64_C(1000000000)

typedef struct Rational {
    int64_t numerator;
    int64_t denominator;
} Rational;

typedef struct Monomial {
    uint8_t powers[ALGEBRA_MAX_VARIABLES];
} Monomial;

typedef struct AlgebraTerm {
    Monomial monomial;
    Rational coefficient;
} AlgebraTerm;

typedef struct Polynomial {
    AlgebraTerm terms[ALGEBRA_MAX_TERMS];
    size_t count;
} Polynomial;

typedef struct Parser {
    const char *source;
    size_t length;
    size_t cursor;
    char variables[ALGEBRA_MAX_VARIABLES][ALGEBRA_MAX_NAME + 1U];
    size_t variable_count;
    TrytrAlgebraStatus error_status;
    char error[160];
} Parser;

void set_error(Parser *parser, TrytrAlgebraStatus status, const char *message);
void skip_space(Parser *parser);
bool consume(Parser *parser, const char *token);

bool rational_normalize(Rational *value);
bool rational_add(Rational left, Rational right, Rational *result);
bool rational_multiply(Rational left, Rational right, Rational *result);
bool rational_divide(Rational left, Rational right, Rational *result);
bool rational_equal(Rational left, Rational right);

Polynomial polynomial_zero(void);
bool monomial_equal(const Monomial *left, const Monomial *right);
bool polynomial_add_term(Parser *parser, Polynomial *polynomial,
                         const Monomial *monomial, Rational coefficient);
Polynomial polynomial_constant(Rational value);
bool polynomial_add(Parser *parser, const Polynomial *left,
                    const Polynomial *right, bool subtract, Polynomial *result);
bool polynomial_multiply(Parser *parser, const Polynomial *left,
                         const Polynomial *right, Polynomial *result);
bool polynomial_scale(Parser *parser, const Polynomial *polynomial,
                      Rational scalar, Polynomial *result);
bool polynomial_power(Parser *parser, const Polynomial *base,
                      uint64_t exponent, Polynomial *result);

#endif
