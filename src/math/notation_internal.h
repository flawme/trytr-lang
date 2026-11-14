#ifndef TRYTR_SRC_MATH_NOTATION_INTERNAL_H
#define TRYTR_SRC_MATH_NOTATION_INTERNAL_H

#include "../arena.h"
#include "trytr/math/notation.h"

typedef enum TrytrMathNotationStatus {
    TRYTR_MATH_NOTATION_OK = 0,
    TRYTR_MATH_NOTATION_NO_MEMORY,
    TRYTR_MATH_NOTATION_LIMIT_EXCEEDED
} TrytrMathNotationStatus;

TrytrMathNotationStatus trytr_math_notation_parse(
    TrytrArena *arena,
    const char *source,
    size_t length,
    const TrytrMathNode **out_root
);

#endif
