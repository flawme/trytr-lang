#ifndef TRYTR_ALGEBRA_H
#define TRYTR_ALGEBRA_H

#include <stddef.h>

typedef enum TrytrAlgebraStatus {
    TRYTR_ALGEBRA_VERIFIED,
    TRYTR_ALGEBRA_NOT_IDENTITY,
    TRYTR_ALGEBRA_UNSUPPORTED,
    TRYTR_ALGEBRA_INVALID,
    TRYTR_ALGEBRA_CONDITIONAL
} TrytrAlgebraStatus;

#define TRYTR_ALGEBRA_MAX_CONDITIONS 32U

typedef struct TrytrAlgebraCondition {
    /* This original-source expression must be defined and nonzero. */
    size_t offset;
    size_t length;
} TrytrAlgebraCondition;

typedef struct TrytrAlgebraResult {
    TrytrAlgebraStatus status;
    size_t offset;
    char message[160];
    size_t condition_count;
    TrytrAlgebraCondition conditions[TRYTR_ALGEBRA_MAX_CONDITIONS];
} TrytrAlgebraResult;

/* Check whether one LaTeX-like equality is a polynomial identity over Q. */
TrytrAlgebraResult trytr_algebra_check(const char *source);

/* Rational-function identities over Q, with explicit original divisor spans.
 * CONDITIONAL is not an unconditional proof: all reported expressions must be
 * defined and nonzero. Cancellation never removes these obligations. Unsupported
 * functions, bounds, or overflow return UNSUPPORTED, not numerical guesses. */
TrytrAlgebraResult trytr_algebra_check_rational(const char *source);

#endif
