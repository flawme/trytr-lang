# Math notation tree

`@math` content currently has two parallel representations. The resolved text
continues to drive the SVG and text renderers. The document API also exposes a
bounded, immutable tree; the native MathML renderer interprets equation
structure directly from that tree.

Include `trytr/document.h` and inspect `TrytrDocumentElement.math_tree` from a
visitor. A non-null tree is provided for `@math` and for the formatted result
of `@expr`:

```c
static bool visit_element(const TrytrDocumentElement *element, void *context)
{
    if (element->kind != TRYTR_ELEMENT_MATH || element->math_tree == NULL) {
        return true;
    }

    const TrytrMathNode *root = element->math_tree;
    /* Visit root and its children while the document remains alive. */
    (void)root;
    (void)context;
    return true;
}
```

Each node has a kind, source span, child count, and ordered child links. The
span fields are byte offsets into the element's `text`, not Unicode character
indices. A node's `operator_offset` and `operator_length` identify an explicit
operator for unary, binary, and relation nodes. Child and text pointers remain
valid until `trytr_document_destroy`; the element view itself is only valid
during its visitor call.

The current structure recognizes:

- Decimal and scientific numeric tokens, identifiers, TeX command tokens, and
  literal text tokens.
- Braced, parenthesized, and square-bracket groups.
- Unary `+` and `-`; explicit `+`, `-`, `*`, `/`, `\\cdot`, `\\times`, and
  `\\div` binary operators; and common relation operators such as `=`, `<`,
  `>=`, `\\neq`, `\\approx`, and Unicode forms such as `≤`, `≠`, and `→`.
- Subscripts and superscripts, `\\sqrt{...}`, and `\\frac{...}{...}`.

Explicit binary operators are grouped by precedence. Juxtaposed notation such
as `2x` or `f(x)` remains separate, ordered content in a `ROW`; the tree does
not infer multiplication or function calls from adjacency. It also does not
evaluate ordinary `@math` equations. That keeps notation parsing separate from
the executable scalar grammar used by `@expr` and formula plots.

Unknown commands and incomplete TeX constructs are retained in the tree as
command or raw source nodes and do not invalidate an otherwise valid document.
Parsing is bounded to 8,192 nodes and 32 levels of recursive structure. If an
expression exceeds the node bound, its tree falls back to one `RAW` root; the
original source and existing rendering behavior are unchanged. The tree is an
initial structured interface, not a full TeX grammar. The MathML output
coverage and remaining limitations are described in
[native math rendering](mathml-rendering.md).

Opt-in heading/text [inline runs](inline-math.md) also use this parser. Their
notation-tree offsets refer to each run's content slice within the paragraph.
Run spans and trees are immutable and share the parsed document's lifetime.

Dedicated adversarial coverage is in `tests/test_math_notation.c`; conversion
and XML-escaping coverage is in `tests/test_mathml.c`. The notation tests check
nested fractions and roots, source-span bounds, unknown and malformed
commands, empty expressions, deep nesting, long unary chains, and bounded
fallback for very long inputs.
