#ifndef TRYTR_INLINE_H
#define TRYTR_INLINE_H

#include "trytr/math/notation.h"

typedef enum TrytrInlineRunKind {
    TRYTR_INLINE_TEXT,
    TRYTR_INLINE_MATH
} TrytrInlineRunKind;

/* Immutable runs owned by the parsed document. Source and content spans are
 * byte offsets into the owning element.text after parameter expansion.
 * Source spans include math delimiters or escaped backslashes. Content spans
 * exclude those delimiters; an escaped backslash has two source bytes and
 * one content byte. Math-tree offsets are relative to this content slice.
 */
typedef struct TrytrInlineRun {
    TrytrInlineRunKind kind;
    size_t source_offset;
    size_t source_length;
    size_t content_offset;
    size_t content_length;
    const TrytrMathNode *math_tree;
    const struct TrytrInlineRun *next;
} TrytrInlineRun;

#endif
