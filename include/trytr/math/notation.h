#ifndef TRYTR_MATH_NOTATION_H
#define TRYTR_MATH_NOTATION_H

#include <stddef.h>

/*
 * Immutable presentation and explicit-operator structure for an @math
 * expression. Children are ordered left to right. BINARY and RELATION nodes
 * have left and right children; UNARY nodes have one operand. Implicitly
 * adjacent symbols remain separate children of a ROW. A GROUP has one ROW
 * child, FRACTION has numerator and denominator GROUP children, and
 * SQUARE_ROOT has one radicand child.
 * Nodes and their source spans are owned by the parsed document and remain
 * valid until that document is destroyed. Offsets are byte offsets into the
 * owning TrytrDocumentElement.text string for display math. Inline run trees
 * instead use offsets into that run's content slice (see trytr/inline.h).
 * operator_offset is zero when the
 * node has no explicit operator.
 * The parser bounds tree size; if that bound is reached, the expression is
 * represented by one RAW node while the original text remains unchanged.
 */
typedef enum TrytrMathNodeKind {
    TRYTR_MATH_NODE_ROW = 0,
    TRYTR_MATH_NODE_IDENTIFIER,
    TRYTR_MATH_NODE_NUMBER,
    TRYTR_MATH_NODE_COMMAND,
    TRYTR_MATH_NODE_TEXT,
    TRYTR_MATH_NODE_GROUP,
    TRYTR_MATH_NODE_FRACTION,
    TRYTR_MATH_NODE_SQUARE_ROOT,
    TRYTR_MATH_NODE_SUBSCRIPT,
    TRYTR_MATH_NODE_SUPERSCRIPT,
    TRYTR_MATH_NODE_UNARY,
    TRYTR_MATH_NODE_BINARY,
    TRYTR_MATH_NODE_RELATION,
    TRYTR_MATH_NODE_RAW
} TrytrMathNodeKind;

typedef struct TrytrMathNode {
    TrytrMathNodeKind kind;
    size_t source_offset;
    size_t source_length;
    size_t operator_offset;
    size_t operator_length;
    size_t child_count;
    const struct TrytrMathNode *first_child;
    const struct TrytrMathNode *next_sibling;
} TrytrMathNode;

#endif
