#include "test.h"

#include "parser.h"
#include "trytr/document.h"

#include <stdlib.h>

static const TrytrDiagnostic *find_diagnostic(
    const TrytrParserOutput *output,
    const char *code
)
{
    for (size_t index = 0u; index < output->diagnostics.count; ++index) {
        if (strcmp(output->diagnostics.items[index].code, code) == 0) {
            return &output->diagnostics.items[index];
        }
    }
    return NULL;
}

static bool test_nested_group_tree_and_integer_translations(void)
{
    static const char source[] =
        "@document(version:1){"
        "@group #outer(10,-3){"
        "@rect #box(1,2,3,4)"
        "@group #inner(2+1,4){@circle #dot(5,6,1)}"
        "}"
        "@text #tail{end}"
        "}";
    TrytrParserOutput output;
    TEST_ASSERT(trytr_parser_parse(source, sizeof(source) - 1u, &output));

    const TrytrNode *outer = output.root->children;
    TEST_ASSERT_INT(TRYTR_NODE_GROUP, outer->kind);
    TEST_ASSERT_STRING("outer", outer->id);
    TEST_ASSERT_INT(10, outer->group_translate_x);
    TEST_ASSERT_INT(-3, outer->group_translate_y);
    TEST_ASSERT_INT(TRYTR_NODE_RECT, outer->children->kind);
    TEST_ASSERT_STRING("box", outer->children->id);
    const TrytrNode *inner = outer->children->next;
    TEST_ASSERT_INT(TRYTR_NODE_GROUP, inner->kind);
    TEST_ASSERT_INT(3, inner->group_translate_x);
    TEST_ASSERT_INT(4, inner->group_translate_y);
    TEST_ASSERT_INT(TRYTR_NODE_CIRCLE, inner->children->kind);
    TEST_ASSERT_INT(TRYTR_NODE_TEXT, outer->next->kind);
    TEST_ASSERT_STRING("tail", outer->next->id);
    trytr_parser_output_destroy(&output);
    return true;
}

static bool test_ids_and_translation_ranges_include_nested_groups(void)
{
    static const char duplicate[] =
        "@document(version:1){@group #same(0,0){@text #inside{x}}@rect #same(0,0,1,1)}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(duplicate, sizeof(duplicate) - 1u, &document));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT, trytr_document_validate(document));
    bool found_duplicate = false;
    for (size_t index = 0u; index < trytr_document_diagnostic_count(document); ++index) {
        const TrytrDiagnostic *diagnostic = trytr_document_diagnostic_at(document, index);
        if (diagnostic != NULL && strcmp(diagnostic->code, "TRYTR-E-DUPLICATE-ID") == 0) {
            found_duplicate = true;
        }
    }
    TEST_ASSERT(found_duplicate);
    trytr_document_destroy(document);

    static const char out_of_range[] =
        "@document(version:1){@group(1000001,0){@rect(0,0,1,1)}}";
    document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(out_of_range, sizeof(out_of_range) - 1u, &document));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT, trytr_document_validate(document));
    bool found_range = false;
    for (size_t index = 0u; index < trytr_document_diagnostic_count(document); ++index) {
        const TrytrDiagnostic *diagnostic = trytr_document_diagnostic_at(document, index);
        if (diagnostic != NULL && strcmp(diagnostic->code, "TRYTR-E-COORDINATE") == 0) {
            found_range = true;
        }
    }
    TEST_ASSERT(found_range);
    trytr_document_destroy(document);
    return true;
}

static bool test_group_nesting_limit(void)
{
    const size_t depth = TRYTR_DEFAULT_DEPTH_LIMIT + 1u;
    static const char prefix[] = "@document(version:1){";
    static const char group[] = "@group(0,0){";
    static const char leaf[] = "@text{x}";
    const size_t source_length = sizeof(prefix) - 1u +
        depth * (sizeof(group) - 1u) + sizeof(leaf) - 1u + depth + 1u;
    char *source = malloc(source_length + 1u);
    TEST_ASSERT(source != NULL);
    char *cursor = source;
    memcpy(cursor, prefix, sizeof(prefix) - 1u);
    cursor += sizeof(prefix) - 1u;
    for (size_t index = 0u; index < depth; ++index) {
        memcpy(cursor, group, sizeof(group) - 1u);
        cursor += sizeof(group) - 1u;
    }
    memcpy(cursor, leaf, sizeof(leaf) - 1u);
    cursor += sizeof(leaf) - 1u;
    for (size_t index = 0u; index < depth + 1u; ++index) *cursor++ = '}';
    *cursor = '\0';
    TEST_ASSERT_SIZE(source_length, (size_t)(cursor - source));

    TrytrParserOutput output;
    TEST_ASSERT(!trytr_parser_parse(source, source_length, &output));
    TEST_ASSERT(find_diagnostic(&output, "TRYTR-E-LIMIT") != NULL);
    trytr_parser_output_destroy(&output);
    free(source);
    return true;
}

static const TrytrTestCase tests[] = {
    {"nested group AST and translations", test_nested_group_tree_and_integer_translations},
    {"nested IDs and coordinate limits", test_ids_and_translation_ranges_include_nested_groups},
    {"group nesting limit", test_group_nesting_limit}
};

TEST_MAIN(tests)
