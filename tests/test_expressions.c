#include "test.h"

#include "parser.h"

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

static int64_t property_value(const TrytrNode *node, const char *name)
{
    for (const TrytrProperty *property = node->properties;
         property != NULL;
         property = property->next) {
        if (strcmp(property->name, name) == 0) return property->integer_value;
    }
    return INT64_MIN;
}

static bool test_precedence_unary_and_integer_division(void)
{
    static const char source[] =
        "@document(version:1){@rect(2+3*4,(2+3)*4,9-2*3,20/3)}";
    TrytrParserOutput output;
    TEST_ASSERT(trytr_parser_parse(source, sizeof(source) - 1u, &output));
    const TrytrNode *rect = output.root->children;
    TEST_ASSERT_INT(14, property_value(rect, "x"));
    TEST_ASSERT_INT(20, property_value(rect, "y"));
    TEST_ASSERT_INT(3, property_value(rect, "width"));
    TEST_ASSERT_INT(6, property_value(rect, "height"));
    trytr_parser_output_destroy(&output);
    return true;
}

static bool test_named_expressions_and_signed_minimum(void)
{
    static const char source[] =
        "@document(version:1){@rect{x:-9223372036854775808 y:+(3*2) width:1 height:2}}";
    TrytrParserOutput output;
    TEST_ASSERT(trytr_parser_parse(source, sizeof(source) - 1u, &output));
    TEST_ASSERT_INT(INT64_MIN, property_value(output.root->children, "x"));
    TEST_ASSERT_INT(6, property_value(output.root->children, "y"));
    trytr_parser_output_destroy(&output);
    return true;
}

static bool test_arithmetic_failures_have_specific_diagnostics(void)
{
    static const struct {
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"@document(version:1){@rect(9223372036854775807+1,0,1,1)}", "TRYTR-E-OVERFLOW"},
        {"@document(version:1){@rect(1,0,4/0,1)}", "TRYTR-E-EXPR"},
        {"@document(version:1){@rect(1,0,-9223372036854775808/-1,1)}", "TRYTR-E-OVERFLOW"},
        {"@document(version:1){@rect(1,0,3037000500*3037000500,1)}", "TRYTR-E-OVERFLOW"}
    };

    for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        TrytrParserOutput output;
        TEST_ASSERT(!trytr_parser_parse(cases[index].source,
                                        strlen(cases[index].source), &output));
        TEST_ASSERT(find_diagnostic(&output, cases[index].diagnostic) != NULL);
        trytr_parser_output_destroy(&output);
    }
    return true;
}

static bool test_expression_nesting_limit(void)
{
    const size_t depth = TRYTR_DEFAULT_EXPRESSION_DEPTH_LIMIT + 1u;
    const char prefix[] = "@document(version:1){@rect(";
    const char suffix[] = ",0,1,1)}";
    const size_t expression_length = depth * 2u + 1u;
    const size_t source_length = sizeof(prefix) - 1u + expression_length + sizeof(suffix) - 1u;
    char *source = malloc(source_length + 1u);
    TEST_ASSERT(source != NULL);

    char *cursor = source;
    memcpy(cursor, prefix, sizeof(prefix) - 1u);
    cursor += sizeof(prefix) - 1u;
    for (size_t index = 0u; index < depth; ++index) *cursor++ = '(';
    *cursor++ = '1';
    for (size_t index = 0u; index < depth; ++index) *cursor++ = ')';
    memcpy(cursor, suffix, sizeof(suffix) - 1u);

    TrytrParserOutput output;
    TEST_ASSERT(!trytr_parser_parse(source, source_length, &output));
    TEST_ASSERT(find_diagnostic(&output, "TRYTR-E-LIMIT") != NULL);
    trytr_parser_output_destroy(&output);
    free(source);
    return true;
}

static bool test_expression_operation_limit(void)
{
    const size_t operations = TRYTR_DEFAULT_EXPRESSION_OPERATION_LIMIT + 1u;
    const char prefix[] = "@document(version:1){@rect(0";
    const char suffix[] = ",0,1,1)}";
    const size_t expression_length = operations * 2u;
    const size_t source_length = sizeof(prefix) - 1u + expression_length + sizeof(suffix) - 1u;
    char *source = malloc(source_length + 1u);
    TEST_ASSERT(source != NULL);

    char *cursor = source;
    memcpy(cursor, prefix, sizeof(prefix) - 1u);
    cursor += sizeof(prefix) - 1u;
    for (size_t index = 0u; index < operations; ++index) {
        *cursor++ = '+';
        *cursor++ = '0';
    }
    memcpy(cursor, suffix, sizeof(suffix) - 1u);

    TrytrParserOutput output;
    TEST_ASSERT(!trytr_parser_parse(source, source_length, &output));
    TEST_ASSERT(find_diagnostic(&output, "TRYTR-E-LIMIT") != NULL);
    trytr_parser_output_destroy(&output);
    free(source);
    return true;
}

static const TrytrTestCase tests[] = {
    {"precedence, unary signs, and integer division", test_precedence_unary_and_integer_division},
    {"named expressions and signed minimum", test_named_expressions_and_signed_minimum},
    {"arithmetic failure diagnostics", test_arithmetic_failures_have_specific_diagnostics},
    {"expression nesting limit", test_expression_nesting_limit},
    {"expression operation limit", test_expression_operation_limit}
};

TEST_MAIN(tests)
