#include "test.h"
#include "trytr/document.h"

#include <stdlib.h>

typedef struct Capture {
    TrytrDocumentElement elements[4];
    size_t count;
} Capture;

static bool capture(const TrytrDocumentElement *element, void *data)
{
    Capture *result = data;
    if (result->count >= 4u) return false;
    result->elements[result->count++] = *element;
    return true;
}

static bool has_code(const TrytrDocument *document, const char *code)
{
    for (size_t index = 0u; index < trytr_document_diagnostic_count(document); ++index) {
        if (strcmp(trytr_document_diagnostic_at(document, index)->code, code) == 0) return true;
    }
    return false;
}

static bool valid_tree_spans(const TrytrMathNode *node, size_t length)
{
    TEST_ASSERT(node != NULL);
    TEST_ASSERT(node->source_offset <= length && node->source_length <= length - node->source_offset);
    TEST_ASSERT(node->operator_offset <= length && node->operator_length <= length - node->operator_offset);
    for (const TrytrMathNode *child = node->first_child; child != NULL; child = child->next_sibling)
        TEST_ASSERT(valid_tree_spans(child, length));
    return true;
}

static bool test_inline_runs_survive_execution_with_precise_spans(void)
{
    const char source[] = "@document(version:1){"
        "@heading(inline-math:true){Set \\(S\\)}"
        "@text #body(inline-math:true){Let \\(x_i^2\\) satisfy \\(x^2 + y^2 = 1\\).}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_parse(source, sizeof(source) - 1u, &document));
    Capture result = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_execute(document, capture, &result));
    TEST_ASSERT_SIZE(2u, result.count);
    TEST_ASSERT(result.elements[0].inline_runs != NULL);
    const TrytrDocumentElement *element = &result.elements[1];
    size_t run_count = 0u, math_count = 0u, cursor = 0u;
    for (const TrytrInlineRun *run = element->inline_runs; run != NULL; run = run->next) {
        TEST_ASSERT_SIZE(cursor, run->source_offset);
        TEST_ASSERT(run->content_offset >= run->source_offset);
        TEST_ASSERT(run->content_offset + run->content_length <= run->source_offset + run->source_length);
        cursor += run->source_length;
        if (run->kind == TRYTR_INLINE_MATH) {
            TEST_ASSERT(element->text[run->source_offset] == '\\');
            TEST_ASSERT(element->text[run->source_offset + 1u] == '(');
            TEST_ASSERT(element->text[cursor - 1u] == ')');
            TEST_ASSERT(valid_tree_spans(run->math_tree, run->content_length));
            ++math_count;
        }
        ++run_count;
    }
    TEST_ASSERT_SIZE(5u, run_count);
    TEST_ASSERT_SIZE(2u, math_count);
    TEST_ASSERT_SIZE(element->text_length, cursor);
    const TrytrInlineRun *formula = element->inline_runs->next;
    TEST_ASSERT_SIZE(4u, formula->source_offset);
    TEST_ASSERT_SIZE(6u, formula->content_offset);
    TEST_ASSERT_SIZE(5u, formula->content_length);
    TEST_ASSERT(memcmp(element->text + formula->content_offset, "x_i^2", 5u) == 0);
    trytr_document_destroy(document);
    return true;
}

static bool test_legacy_and_explicitly_disabled_text_remain_literal(void)
{
    const char source[] = "@document(version:1){"
        "@text{Pay $5 and keep \\(unfinished.}"
        "@text(inline-math:false){Keep \\(x\\) literal.}@text(inline-math:true){}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_parse(source, sizeof(source) - 1u, &document));
    Capture result = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_execute(document, capture, &result));
    TEST_ASSERT_SIZE(3u, result.count);
    TEST_ASSERT(result.elements[0].inline_runs == NULL);
    TEST_ASSERT(result.elements[1].inline_runs == NULL);
    TEST_ASSERT(result.elements[2].inline_runs == NULL);
    TEST_ASSERT_STRING("Pay $5 and keep \\(unfinished.", result.elements[0].text);
    trytr_document_destroy(document);
    return true;
}

static bool test_invalid_fences_are_located_before_parameter_expansion(void)
{
    static const char *const contents[] = {
        "before \\(x", "before \\) after", "before \\(\\)", "before \\( \t\\)",
        "before \\(x\ny\\)", "before \\(x\ry\\)", "before \\(x \\(y\\)\\)",
        "before \\(x\\\ny\\)", "before \\(x\\\ry\\)"
    };
    for (size_t index = 0u; index < sizeof(contents) / sizeof(contents[0]); ++index) {
        char source[256];
        const int size = snprintf(source, sizeof(source),
            "@document(version:1){@param #p(min:0,max:999,value:999,step:1){P}"
            "@text(inline-math:true){\n  π #p %s}}", contents[index]);
        TEST_ASSERT(size > 0 && (size_t)size < sizeof(source));
        TrytrDocument *document = NULL;
        TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                        trytr_document_parse(source, (size_t)size, &document));
        const TrytrDiagnostic *diagnostic = trytr_document_diagnostic_at(document, 0u);
        TEST_ASSERT(diagnostic != NULL);
        TEST_ASSERT_STRING("TRYTR-E-INLINE-MATH", diagnostic->code);
        TEST_ASSERT_SIZE((size_t)(strchr(source, '\\') - source), diagnostic->location.offset);
        TEST_ASSERT_SIZE(2u, diagnostic->location.line);
        TEST_ASSERT_SIZE(15u, diagnostic->location.column);
        trytr_document_destroy(document);
    }
    return true;
}

static bool test_inline_options_and_unknown_parameters_are_rejected(void)
{
    static const struct { const char *source; const char *code; } cases[] = {
        {"@document(version:1){@text(inline-math:2){x}}", "TRYTR-E-INLINE-MATH"},
        {"@document(version:1){@text(inline-math:true,inline-math:false){x}}", "TRYTR-E-PROPERTY"},
        {"@document(version:1){@math(inline-math:true){x}}", "TRYTR-E-PROPERTY"},
        {"@document(version:1){@text(inline-math:true){Let \\(#missing\\).}}", "TRYTR-E-PARAMETER"}
    };
    for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        TrytrDocument *document = NULL;
        TrytrStatus status = trytr_document_parse(cases[index].source, strlen(cases[index].source), &document);
        if (status == TRYTR_STATUS_OK) status = trytr_document_validate(document);
        TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT, status);
        TEST_ASSERT(has_code(document, cases[index].code));
        trytr_document_destroy(document);
    }
    return true;
}

static bool test_overrides_rebuild_inline_tree_and_spans(void)
{
    const char source[] = "@document(version:1){"
        "@param #n(min:0,max:999,value:2,step:1){N}"
        "@text(inline-math:true){#n: \\(#n^2\\).}}";
    const TrytrParameterOverride override = {.id = "n", .value = 999};
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_parse_with_parameters(
        source, sizeof(source) - 1u, &override, 1u, &document));
    Capture result = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_execute(document, capture, &result));
    const TrytrInlineRun *run = result.elements[0].inline_runs->next;
    TEST_ASSERT_STRING("999: \\(999^2\\).", result.elements[0].text);
    TEST_ASSERT_SIZE(5u, run->source_offset);
    TEST_ASSERT_SIZE(7u, run->content_offset);
    TEST_ASSERT_SIZE(5u, run->content_length);
    TEST_ASSERT(valid_tree_spans(run->math_tree, run->content_length));
    trytr_document_destroy(document);
    return true;
}

static bool test_document_run_budget_is_shared_between_paragraphs(void)
{
    const char *prefix = "@document(version:1){@text(inline-math:true){";
    const char *separator = "}@text(inline-math:true){";
    const char *piece = "\\(x\\) ";
    const size_t count = 1025u;
    const size_t capacity = strlen(prefix) + strlen(separator) + count * 2u * strlen(piece) + 3u;
    char *source = malloc(capacity);
    TEST_ASSERT(source != NULL);
    strcpy(source, prefix);
    char *cursor = source + strlen(prefix);
    for (size_t block = 0u; block < 2u; ++block) {
        for (size_t index = 0u; index < count; ++index) {
            memcpy(cursor, piece, strlen(piece));
            cursor += strlen(piece);
        }
        if (block == 0u) { memcpy(cursor, separator, strlen(separator)); cursor += strlen(separator); }
    }
    memcpy(cursor, "}}", 3u);
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_LIMIT_EXCEEDED, trytr_document_parse(source, strlen(source), &document));
    TEST_ASSERT(has_code(document, "TRYTR-E-LIMIT"));
    trytr_document_destroy(document);
    free(source);
    return true;
}

static const TrytrTestCase tests[] = {
    {"document-owned runs and precise spans", test_inline_runs_survive_execution_with_precise_spans},
    {"legacy and disabled text compatibility", test_legacy_and_explicitly_disabled_text_remain_literal},
    {"raw source diagnostic locations", test_invalid_fences_are_located_before_parameter_expansion},
    {"invalid options and unknown parameters", test_inline_options_and_unknown_parameters_are_rejected},
    {"parameter overrides rebuild spans", test_overrides_rebuild_inline_tree_and_spans},
    {"document run budget", test_document_run_budget_is_shared_between_paragraphs}
};
TEST_MAIN(tests)
