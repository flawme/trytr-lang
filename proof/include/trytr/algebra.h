#ifndef TRYTR_ALGEBRA_H
#define TRYTR_ALGEBRA_H

#include <stddef.h>

typedef enum TrytrAlgebraStatus {
    TRYTR_ALGEBRA_VERIFIED,
    TRYTR_ALGEBRA_NOT_IDENTITY,
    TRYTR_ALGEBRA_UNSUPPORTED,
    TRYTR_ALGEBRA_INVALID
} TrytrAlgebraStatus;

typedef struct TrytrAlgebraResult {
    TrytrAlgebraStatus status;
    size_t offset;
    char message[160];
} TrytrAlgebraResult;

/* Check whether one LaTeX-like equality is a polynomial identity over Q. */
TrytrAlgebraResult trytr_algebra_check(const char *source);

#endif
