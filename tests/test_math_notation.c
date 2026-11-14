#include "trytr/document.h"

#include "test.h"

#include <stdlib.h>
#include <string.h>

typedef struct MathCapture {
    const TrytrMathNode *tree;
    const char *text;
    size_t text_length;
    bool saw_other_math;
} MathCapture;

static bool capture_math(const TrytrDocumentElement *element, void *user_data)
{
    MathCapture *capture = user_data;
    if (element->kind != TRYTR_ELEMENT_MATH) return true;
    if (capture->tree != NULL) capture->saw_other_math = true;
    capture->tree = element->math_tree;
    capture->text = element->text;
    capture->text_length = element->text_length;
    return true;
}

static const TrytrMathNode *child_at(const TrytrMathNode *node, size_t index)
{
    if (node == NULL || index >= node->child_count) return NULL;
    const TrytrMathNode *child = node->first_child;
    while (index-- != 0u && child != NULL) child = child->next_sibling;
    return child;
}

static bool spans_are_in_bounds(const TrytrMathNode *node, size_t text_length,
                                size_t depth)
{
    if (node == NULL) return true;
    if (depth > 128u || node->source_offset > text_length ||
        node->source_length > text_length - node->source_offset) return false;
    if (node->operator_length != 0u &&
        (node->operator_offset > text_length ||
         node->operator_length > text_length - node->operator_offset)) return false;
    size_t count = 0u;
    for (const TrytrMathNode *child = node->first_child; child != NULL;
         child = child->next_sibling) {
        if (!spans_are_in_bounds(child, text_length, depth + 1u)) return false;
        if (++count > node->child_count) return false;
    }
    return count == node->child_count;
}

static bool parse_math(const char *source, TrytrDocument **out_document,
                       MathCapture *capture)
{
    *out_document = NULL;
    const TrytrStatus status = trytr_document_parse(source, strlen(source), out_document);
    if (status != TRYTR_STATUS_OK) return false;
    return trytr_document_execute(*out_document, capture_math, capture) == TRYTR_STATUS_OK;
}

static bool test_math_tree_preserves_nested_structure_and_spans(void)
{
    const char *source =
        "@document(version:1){@math #formula{\\frac{a+b}{\\sqrt{x_1}} = y^2}}";
    TrytrDocument *document = NULL;
    MathCapture capture = {0};
    TEST_ASSERT(parse_math(source, &document, &capture));
    TEST_ASSERT(capture.tree != NULL);
    TEST_ASSERT(!capture.saw_other_math);
    const TrytrMathNode *row = capture.tree;
    TEST_ASSERT_INT(TRYTR_MATH_NODE_ROW, row->kind);
    TEST_ASSERT_SIZE(1u, row->child_count);

    const TrytrMathNode *equation = child_at(row, 0u);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_RELATION, equation->kind);
    TEST_ASSERT_SIZE(2u, equation->child_count);
    TEST_ASSERT_SIZE(1u, equation->operator_length);
    TEST_ASSERT(capture.text[equation->operator_offset] == '=');

    const TrytrMathNode *fraction = child_at(equation, 0u);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_FRACTION, fraction->kind);
    TEST_ASSERT_SIZE(2u, fraction->child_count);
    const TrytrMathNode *numerator_group = child_at(fraction, 0u);
    const TrytrMathNode *denominator_group = child_at(fraction, 1u);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_GROUP, numerator_group->kind);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_GROUP, denominator_group->kind);
    const TrytrMathNode *numerator = child_at(numerator_group, 0u);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_ROW, numerator->kind);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_BINARY, child_at(numerator, 0u)->kind);

    const TrytrMathNode *denominator = child_at(denominator_group, 0u);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_ROW, denominator->kind);
    const TrytrMathNode *root = child_at(denominator, 0u);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_SQUARE_ROOT, root->kind);
    const TrytrMathNode *radicand_group = child_at(root, 0u);
    const TrytrMathNode *radicand = child_at(radicand_group, 0u);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_SUBSCRIPT,
                    child_at(radicand, 0u)->kind);

    const TrytrMathNode *power = child_at(equation, 1u);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_SUPERSCRIPT, power->kind);
    TEST_ASSERT(spans_are_in_bounds(row, capture.text_length, 0u));
    trytr_document_destroy(document);
    return true;
}

static bool test_operator_precedence_and_unicode_relations(void)
{
    const char *source = "@document(version:1){@math{a + b * c ≤ d}}";
    TrytrDocument *document = NULL;
    MathCapture capture = {0};
    TEST_ASSERT(parse_math(source, &document, &capture));
    const TrytrMathNode *equation = child_at(capture.tree, 0u);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_RELATION, equation->kind);
    TEST_ASSERT_SIZE(3u, equation->operator_length);
    TEST_ASSERT(memcmp(capture.text + equation->operator_offset,
                       "\xE2\x89\xA4", 3u) == 0);
    const TrytrMathNode *sum = child_at(equation, 0u);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_BINARY, sum->kind);
    TEST_ASSERT(capture.text[sum->operator_offset] == '+');
    const TrytrMathNode *product = child_at(sum, 1u);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_BINARY, product->kind);
    TEST_ASSERT(capture.text[product->operator_offset] == '*');
    TEST_ASSERT(spans_are_in_bounds(capture.tree, capture.text_length, 0u));
    trytr_document_destroy(document);

    const char *compact = "@document(version:1){@math{a×b}}";
    document = NULL;
    memset(&capture, 0, sizeof(capture));
    TEST_ASSERT(parse_math(compact, &document, &capture));
    const TrytrMathNode *compact_product = child_at(capture.tree, 0u);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_BINARY, compact_product->kind);
    TEST_ASSERT_SIZE(2u, compact_product->operator_length);
    trytr_document_destroy(document);
    return true;
}

static bool test_adjacent_subscript_and_superscript_attach_to_one_base(void)
{
    const char *source = "@document(version:1){@math{z_i^2}}";
    TrytrDocument *document = NULL;
    MathCapture capture = {0};
    TEST_ASSERT(parse_math(source, &document, &capture));
    const TrytrMathNode *superscript = child_at(capture.tree, 0u);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_SUPERSCRIPT, superscript->kind);
    TEST_ASSERT_SIZE(2u, superscript->child_count);
    const TrytrMathNode *subscript = child_at(superscript, 0u);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_SUBSCRIPT, subscript->kind);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_IDENTIFIER, child_at(subscript, 0u)->kind);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_IDENTIFIER, child_at(subscript, 1u)->kind);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_NUMBER, child_at(superscript, 1u)->kind);
    TEST_ASSERT(spans_are_in_bounds(capture.tree, capture.text_length, 0u));
    trytr_document_destroy(document);
    return true;
}

static bool test_unknown_commands_and_incomplete_fraction_stay_representable(void)
{
    const char *source =
        "@document(version:1){@math{\\unknown{a} + \\approximate + \\frac{b}}}";
    TrytrDocument *document = NULL;
    MathCapture capture = {0};
    TEST_ASSERT(parse_math(source, &document, &capture));
    TEST_ASSERT(capture.tree != NULL);
    const TrytrMathNode *row = capture.tree;
    TEST_ASSERT_INT(TRYTR_MATH_NODE_ROW, row->kind);
    bool saw_command = false;
    for (const TrytrMathNode *node = row->first_child; node != NULL;
         node = node->next_sibling) {
        if (node->kind == TRYTR_MATH_NODE_COMMAND) saw_command = true;
    }
    TEST_ASSERT(saw_command);
    TEST_ASSERT(spans_are_in_bounds(row, capture.text_length, 0u));
    trytr_document_destroy(document);

    const char *trailing_operator = "@document(version:1){@math{x+}}";
    document = NULL;
    memset(&capture, 0, sizeof(capture));
    TEST_ASSERT(parse_math(trailing_operator, &document, &capture));
    row = capture.tree;
    const TrytrMathNode *last = row->first_child;
    while (last != NULL && last->next_sibling != NULL) last = last->next_sibling;
    TEST_ASSERT(last != NULL);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_RAW, last->kind);
    TEST_ASSERT(spans_are_in_bounds(row, capture.text_length, 0u));
    trytr_document_destroy(document);
    return true;
}

static bool test_empty_expression_and_bounded_nesting(void)
{
    const char *empty = "@document(version:1){@math{}}";
    TrytrDocument *document = NULL;
    MathCapture capture = {0};
    TEST_ASSERT(parse_math(empty, &document, &capture));
    TEST_ASSERT(capture.tree != NULL);
    TEST_ASSERT_SIZE(0u, capture.tree->child_count);
    trytr_document_destroy(document);

    const char *computed = "@document(version:1){@expr{sqrt(2)}}";
    document = NULL;
    memset(&capture, 0, sizeof(capture));
    TEST_ASSERT(parse_math(computed, &document, &capture));
    TEST_ASSERT(capture.tree != NULL);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_NUMBER,
                    child_at(capture.tree, 0u)->kind);
    trytr_document_destroy(document);

    const char *decimal = "@document(version:1){@math{.5e-2}}";
    document = NULL;
    memset(&capture, 0, sizeof(capture));
    TEST_ASSERT(parse_math(decimal, &document, &capture));
    TEST_ASSERT_INT(TRYTR_MATH_NODE_NUMBER,
                    child_at(capture.tree, 0u)->kind);
    trytr_document_destroy(document);

    const char *unicode_root = "@document(version:1){@math{√2}}";
    document = NULL;
    memset(&capture, 0, sizeof(capture));
    TEST_ASSERT(parse_math(unicode_root, &document, &capture));
    const TrytrMathNode *root = child_at(capture.tree, 0u);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_SQUARE_ROOT, root->kind);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_NUMBER, child_at(root, 0u)->kind);
    trytr_document_destroy(document);

    char nested[256];
    size_t position = 0u;
    const char *prefix = "@document(version:1){@math{";
    memcpy(nested + position, prefix, strlen(prefix));
    position += strlen(prefix);
    for (size_t index = 0u; index < 48u; ++index) nested[position++] = '{';
    nested[position++] = 'x';
    for (size_t index = 0u; index < 48u; ++index) nested[position++] = '}';
    nested[position++] = '}';
    nested[position++] = '}';
    nested[position] = '\0';
    document = NULL;
    memset(&capture, 0, sizeof(capture));
    TEST_ASSERT(parse_math(nested, &document, &capture));
    TEST_ASSERT(capture.tree != NULL);
    TEST_ASSERT(spans_are_in_bounds(capture.tree, capture.text_length, 0u));
    trytr_document_destroy(document);
    return true;
}

static bool test_long_unary_chain_is_bounded(void)
{
    const char *prefix = "@document(version:1){@math{";
    const size_t operators = 2048u;
    const size_t prefix_length = strlen(prefix);
    char *source = malloc(prefix_length + operators + 4u);
    TEST_ASSERT(source != NULL);
    memcpy(source, prefix, prefix_length);
    memset(source + prefix_length, '-', operators);
    source[prefix_length + operators] = 'x';
    source[prefix_length + operators + 1u] = '}';
    source[prefix_length + operators + 2u] = '}';
    source[prefix_length + operators + 3u] = '\0';
    TrytrDocument *document = NULL;
    MathCapture capture = {0};
    const bool parsed = parse_math(source, &document, &capture);
    free(source);
    TEST_ASSERT(parsed);
    TEST_ASSERT(capture.tree != NULL);
    TEST_ASSERT(spans_are_in_bounds(capture.tree, capture.text_length, 0u));
    trytr_document_destroy(document);
    return true;
}

static bool test_notation_node_limit_preserves_pathological_source(void)
{
    const char *prefix = "@document(version:1){@math{";
    const size_t tokens = 8200u;
    const size_t prefix_length = strlen(prefix);
    char *source = malloc(prefix_length + tokens * 2u + 3u);
    TEST_ASSERT(source != NULL);
    memcpy(source, prefix, prefix_length);
    size_t position = prefix_length;
    for (size_t index = 0u; index < tokens; ++index) {
        source[position++] = 'x';
        source[position++] = ' ';
    }
    source[position++] = '}';
    source[position++] = '}';
    source[position] = '\0';
    TrytrDocument *document = NULL;
    const TrytrStatus status = trytr_document_parse(source, position, &document);
    free(source);
    TEST_ASSERT(status == TRYTR_STATUS_OK);
    TEST_ASSERT(document != NULL);
    MathCapture capture = {0};
    TEST_ASSERT(trytr_document_execute(document, capture_math, &capture) ==
                TRYTR_STATUS_OK);
    TEST_ASSERT(capture.tree != NULL);
    TEST_ASSERT_SIZE(0u, capture.tree->child_count);
    TEST_ASSERT_INT(TRYTR_MATH_NODE_RAW, capture.tree->kind);
    TEST_ASSERT_SIZE(capture.text_length, capture.tree->source_length);
    TEST_ASSERT(spans_are_in_bounds(capture.tree, capture.text_length, 0u));
    trytr_document_destroy(document);
    return true;
}

static const TrytrTestCase tests[] = {
    {"nested math structure and source spans", test_math_tree_preserves_nested_structure_and_spans},
    {"operator precedence and Unicode relations", test_operator_precedence_and_unicode_relations},
    {"adjacent subscript and superscript", test_adjacent_subscript_and_superscript_attach_to_one_base},
    {"unknown and malformed math notation", test_unknown_commands_and_incomplete_fraction_stay_representable},
    {"empty notation and bounded nesting", test_empty_expression_and_bounded_nesting},
    {"bounded unary nesting", test_long_unary_chain_is_bounded},
    {"bounded notation preserves source", test_notation_node_limit_preserves_pathological_source}
};

TEST_MAIN(tests)
