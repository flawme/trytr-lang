#ifndef TRYTR_INLINE_INTERNAL_H
#define TRYTR_INLINE_INTERNAL_H

#include "../arena.h"
#include "trytr/inline.h"

typedef enum TrytrInlineStatus {
    TRYTR_INLINE_OK,
    TRYTR_INLINE_INVALID,
    TRYTR_INLINE_LIMIT_EXCEEDED,
    TRYTR_INLINE_NO_MEMORY
} TrytrInlineStatus;

/* NULL arena performs validation/counting only, with no allocations. */
TrytrInlineStatus trytr_inline_parse(
    TrytrArena *arena, const char *source, size_t length, size_t run_limit,
    const TrytrInlineRun **out_runs, size_t *out_count, size_t *out_error_offset);

#endif
