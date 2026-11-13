#include "test.h"

#include "parser.h"

#include <stdint.h>
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

static size_t child_count(const TrytrNode *root)
{
    size_t count = 0u;
    for (const TrytrNode *node = root == NULL ? NULL : root->children;
         node != NULL;
         node = node->next) {
        ++count;
    }
    return count;
}

static const TrytrProperty *find_property(const TrytrNode *node, const char *name)
{
    for (const TrytrProperty *property = node == NULL ? NULL : node->properties;
         property != NULL;
         property = property->next) {
        if (strcmp(property->name, name) == 0) return property;
    }
    return NULL;
}

static bool assert_node_text(const TrytrNode *node, const char *expected)
{
    const size_t expected_length = strlen(expected);
    TEST_ASSERT(node != NULL);
    TEST_ASSERT_SIZE(expected_length, node->text_length);
    TEST_ASSERT(expected_length == 0u || memcmp(node->text, expected, expected_length) == 0);
    TEST_ASSERT_INT('\0', node->text[expected_length]);
    return true;
}

static bool test_full_illustrative_document(void)
{
    static const char source[] =
        "@document(version: 1) {\n"
        "    @heading #title {\n"
        "        JFET Characteristics\n"
        "    }\n"
        "    @math #equation1 {\n"
        "        I_D = I_{DSS}(1 - V_{GS}/V_P)^2\n"
        "    }\n"
        "    @rect(10, 20, 100, 50)\n"
        "    @text {\n"
        "        The output characteristic of an N-channel JFET.\n"
        "    }\n"
        "}";
    TrytrParserOutput output;

    TEST_ASSERT(trytr_parser_parse(source, sizeof(source) - 1u, &output));
    TEST_ASSERT(output.root != NULL);
    TEST_ASSERT_INT(TRYTR_NODE_DOCUMENT, output.root->kind);
    TEST_ASSERT_INT(1, output.root->version);
    TEST_ASSERT_SIZE(0u, output.diagnostics.count);
    TEST_ASSERT_SIZE(4u, child_count(output.root));

    const TrytrNode *heading = output.root->children;
    TEST_ASSERT(heading != NULL);
    TEST_ASSERT_INT(TRYTR_NODE_HEADING, heading->kind);
    TEST_ASSERT_STRING("title", heading->id);
    TEST_ASSERT(assert_node_text(heading, "JFET Characteristics"));
    TEST_ASSERT_SIZE(2u, heading->location.line);
    TEST_ASSERT_SIZE(5u, heading->location.column);

    const TrytrNode *math = heading->next;
    TEST_ASSERT(math != NULL);
    TEST_ASSERT_INT(TRYTR_NODE_MATH, math->kind);
    TEST_ASSERT_STRING("equation1", math->id);
    TEST_ASSERT(assert_node_text(math, "I_D = I_{DSS}(1 - V_{GS}/V_P)^2"));

    const TrytrNode *rect = math->next;
    TEST_ASSERT(rect != NULL);
    TEST_ASSERT_INT(TRYTR_NODE_RECT, rect->kind);
    TEST_ASSERT(rect->id == NULL);
    TEST_ASSERT_INT(10, find_property(rect, "x")->integer_value);
    TEST_ASSERT_INT(20, find_property(rect, "y")->integer_value);
    TEST_ASSERT_INT(100, find_property(rect, "width")->integer_value);
    TEST_ASSERT_INT(50, find_property(rect, "height")->integer_value);

    const TrytrNode *text = rect->next;
    TEST_ASSERT(text != NULL);
    TEST_ASSERT_INT(TRYTR_NODE_TEXT, text->kind);
    TEST_ASSERT(assert_node_text(text, "The output characteristic of an N-channel JFET."));
    TEST_ASSERT(text->next == NULL);
    TEST_ASSERT(output.root->children_tail == text);
    trytr_parser_output_destroy(&output);
    return true;
}

static bool test_signed_positional_and_named_rect_values(void)
{
    static const char positional[] = "@document(version:1){@rect #box(-1,+2,-3,4)}";
    TrytrParserOutput output;
    TEST_ASSERT(trytr_parser_parse(positional, sizeof(positional) - 1u, &output));
    const TrytrNode *rect = output.root->children;
    TEST_ASSERT_STRING("box", rect->id);
    TEST_ASSERT_INT(-1, find_property(rect, "x")->integer_value);
    TEST_ASSERT_INT(2, find_property(rect, "y")->integer_value);
    TEST_ASSERT_INT(-3, find_property(rect, "width")->integer_value);
    TEST_ASSERT_INT(4, find_property(rect, "height")->integer_value);
    trytr_parser_output_destroy(&output);

    static const char named[] =
        "@document(version:1){@rect{x:-1 y:+2, width:30, height:40}}";
    TEST_ASSERT(trytr_parser_parse(named, sizeof(named) - 1u, &output));
    rect = output.root->children;
    TEST_ASSERT_INT(-1, find_property(rect, "x")->integer_value);
    TEST_ASSERT_INT(2, find_property(rect, "y")->integer_value);
    TEST_ASSERT_INT(30, find_property(rect, "width")->integer_value);
    TEST_ASSERT_INT(40, find_property(rect, "height")->integer_value);
    trytr_parser_output_destroy(&output);
    return true;
}

static bool test_document_parses_fixed_canvas_options(void)
{
    static const char source[] =
        "@document(version:1,width:640,height:480){@text{Canvas}}";
    TrytrParserOutput output;
    TEST_ASSERT(trytr_parser_parse(source, sizeof(source) - 1u, &output));
    TEST_ASSERT_INT(1, output.root->version);
    TEST_ASSERT(output.root->has_canvas_width);
    TEST_ASSERT(output.root->has_canvas_height);
    TEST_ASSERT_INT(640, output.root->canvas_width);
    TEST_ASSERT_INT(480, output.root->canvas_height);
    trytr_parser_output_destroy(&output);

    static const char duplicate[] =
        "@document(version:1,width:640,width:800,height:480){}";
    TEST_ASSERT(!trytr_parser_parse(duplicate, sizeof(duplicate) - 1u, &output));
    TEST_ASSERT(find_diagnostic(&output, "TRYTR-E-PROPERTY") != NULL);
    trytr_parser_output_destroy(&output);
    return true;
}

static bool test_raw_blocks_preserve_contents_and_trim_outer_space(void)
{
    static const char source[] =
        "@document(version:1){@text{ \n\t }@heading{// not a comment /* either */}}";
    TrytrParserOutput output;
    TEST_ASSERT(trytr_parser_parse(source, sizeof(source) - 1u, &output));
    TEST_ASSERT_SIZE(2u, child_count(output.root));
    TEST_ASSERT(assert_node_text(output.root->children, ""));
    TEST_ASSERT(assert_node_text(output.root->children->next, "// not a comment /* either */"));
    trytr_parser_output_destroy(&output);
    return true;
}

static bool test_unknown_element_recovers_at_later_nodes(void)
{
    static const char source[] =
        "@document(version:1){"
        "@unknown #skip { arbitrary {nested} @text{not a real node} }"
        "@text #ok { later }@rect(1,2,3,4)}";
    TrytrParserOutput output;
    TEST_ASSERT(!trytr_parser_parse(source, sizeof(source) - 1u, &output));
    TEST_ASSERT(find_diagnostic(&output, "TRYTR-E-ELEMENT") != NULL);
    TEST_ASSERT_SIZE(2u, child_count(output.root));
    TEST_ASSERT_INT(TRYTR_NODE_TEXT, output.root->children->kind);
    TEST_ASSERT_STRING("ok", output.root->children->id);
    TEST_ASSERT(assert_node_text(output.root->children, "later"));
    TEST_ASSERT_INT(TRYTR_NODE_RECT, output.root->children_tail->kind);
    trytr_parser_output_destroy(&output);
    return true;
}

static bool test_malformed_documents_have_diagnostics(void)
{
    static const char *const cases[] = {
        "@document(version: 1 { @text{} }",
        "@document(version:1){@text{broken",
        "@document(version:1){@text{}",
        "@document(version:1){@rect(1,2,3)}",
        "@document(version:1){@rect(1,2,3,4,5)}",
        "@document(version:1){@rect{x 1 y:2 width:3 height:4}}",
        "@document(version:1){} @text{}",
        "@document(version:0){}",
        "@document(version:-1){}",
        "@document(version:9223372036854775808){}",
        "@document(version:1){@rect(1.0,2,3,4)}",
        "@document(version:1){@text #9bad{x}}"
    };

    for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        TrytrParserOutput output;
        TEST_ASSERT(!trytr_parser_parse(cases[index], strlen(cases[index]), &output));
        TEST_ASSERT(output.diagnostics.count > 0u);
        trytr_parser_output_destroy(&output);
    }
    return true;
}

static bool test_path_is_parsed_and_invalid_commands_are_located(void)
{
    static const char source[] = "@document(version:1){@path #curve{ M 1 2 L 3 4 }}";
    TrytrParserOutput output;
    TEST_ASSERT(trytr_parser_parse(source, sizeof(source) - 1u, &output));
    TEST_ASSERT_INT(TRYTR_NODE_PATH, output.root->children->kind);
    TEST_ASSERT_STRING("curve", output.root->children->id);
    TEST_ASSERT_SIZE(2u, output.root->children->path_segment_count);
    TEST_ASSERT_INT(1, output.root->children->path_min_x);
    TEST_ASSERT_INT(2, output.root->children->path_min_y);
    TEST_ASSERT_INT(3, output.root->children->path_max_x);
    TEST_ASSERT_INT(4, output.root->children->path_max_y);
    trytr_parser_output_destroy(&output);

    static const char invalid[] = "@document(version:1){@path{ \n  M 1 2 A 3 4}}";
    TEST_ASSERT(!trytr_parser_parse(invalid, sizeof(invalid) - 1u, &output));
    const TrytrDiagnostic *diagnostic = find_diagnostic(&output, "TRYTR-E-PATH");
    TEST_ASSERT(diagnostic != NULL);
    TEST_ASSERT_SIZE((size_t)(strstr(invalid, "A 3 4") - invalid),
                     diagnostic->location.offset);
    trytr_parser_output_destroy(&output);
    return true;
}

static bool test_source_and_nesting_limits(void)
{
    const size_t source_length = TRYTR_DEFAULT_SOURCE_LIMIT + 1u;
    char *source = malloc(source_length);
    TEST_ASSERT(source != NULL);
    memset(source, ' ', source_length);
    TrytrParserOutput output;
    TEST_ASSERT(!trytr_parser_parse(source, source_length, &output));
    TEST_ASSERT(find_diagnostic(&output, "TRYTR-E-LIMIT") != NULL);
    trytr_parser_output_destroy(&output);
    free(source);

    static const char prefix[] = "@document(version:1){@math{";
    const size_t nested = 64u;
    const size_t length = sizeof(prefix) - 1u + nested * 2u + 3u;
    source = malloc(length + 1u);
    TEST_ASSERT(source != NULL);
    char *cursor = source;
    memcpy(cursor, prefix, sizeof(prefix) - 1u);
    cursor += sizeof(prefix) - 1u;
    for (size_t index = 0u; index < nested; ++index) *cursor++ = '{';
    *cursor++ = 'x';
    for (size_t index = 0u; index < nested; ++index) *cursor++ = '}';
    *cursor++ = '}';
    *cursor++ = '}';
    *cursor = '\0';
    TEST_ASSERT(!trytr_parser_parse(source, length, &output));
    TEST_ASSERT(find_diagnostic(&output, "TRYTR-E-LIMIT") != NULL);
    trytr_parser_output_destroy(&output);
    free(source);
    return true;
}

static bool test_node_and_diagnostic_limits(void)
{
    static const char prefix[] = "@document(version:1){";
    static const char element[] = "@text{}";
    const size_t count = TRYTR_DEFAULT_NODE_LIMIT;
    const size_t length = sizeof(prefix) - 1u + count * (sizeof(element) - 1u) + 1u;
    char *source = malloc(length + 1u);
    TEST_ASSERT(source != NULL);
    char *cursor = source;
    memcpy(cursor, prefix, sizeof(prefix) - 1u);
    cursor += sizeof(prefix) - 1u;
    for (size_t index = 0u; index < count; ++index) {
        memcpy(cursor, element, sizeof(element) - 1u);
        cursor += sizeof(element) - 1u;
    }
    *cursor++ = '}';
    *cursor = '\0';
    TrytrParserOutput output;
    TEST_ASSERT(!trytr_parser_parse(source, length, &output));
    TEST_ASSERT(find_diagnostic(&output, "TRYTR-E-LIMIT") != NULL);
    trytr_parser_output_destroy(&output);
    free(source);

    static const char bad_elements[] = "@document(version:1){@unknown@unknown@unknown@unknown";
    TEST_ASSERT(!trytr_parser_parse(bad_elements, sizeof(bad_elements) - 1u, &output));
    TEST_ASSERT(output.diagnostics.count > 0u);
    trytr_parser_output_destroy(&output);
    return true;
}

static const TrytrTestCase tests[] = {
    {"full illustrative document", test_full_illustrative_document},
    {"signed positional and named rectangles", test_signed_positional_and_named_rect_values},
    {"fixed canvas document options", test_document_parses_fixed_canvas_options},
    {"raw blocks", test_raw_blocks_preserve_contents_and_trim_outer_space},
    {"unknown element recovery", test_unknown_element_recovers_at_later_nodes},
    {"malformed documents", test_malformed_documents_have_diagnostics},
    {"path parsing and diagnostics", test_path_is_parsed_and_invalid_commands_are_located},
    {"source and nesting limits", test_source_and_nesting_limits},
    {"node and diagnostic limits", test_node_and_diagnostic_limits}
};

TEST_MAIN(tests)
