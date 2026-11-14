#include "trytr/math/mathml.h"

#include "test.h"

#include <stdlib.h>
#include <string.h>

static bool render(const char *source, char **out_mathml, size_t *out_length)
{
    TrytrDocument *document = NULL;
    const TrytrStatus status = trytr_document_parse(source, strlen(source), &document);
    if (document == NULL || status != TRYTR_STATUS_OK) {
        trytr_document_destroy(document);
        return false;
    }
    const TrytrStatus render_status = trytr_mathml_render(document, out_mathml,
                                                         out_length);
    trytr_document_destroy(document);
    return render_status == TRYTR_STATUS_OK;
}

static bool contains(const char *text, const char *fragment)
{
    return text != NULL && strstr(text, fragment) != NULL;
}

static size_t count(const char *text, const char *fragment)
{
    size_t matches = 0u;
    const size_t fragment_length = strlen(fragment);
    const char *cursor = text;
    while ((cursor = strstr(cursor, fragment)) != NULL) {
        ++matches;
        cursor += fragment_length;
    }
    return matches;
}

static bool test_mathml_uses_semantic_fraction_root_and_scripts(void)
{
    char *mathml = NULL;
    size_t length = 0u;
    TEST_ASSERT(render(
        "@document(version:1){@math{\\frac{\\alpha+x_1}{\\sqrt{y}}=z_i^2}}",
        &mathml, &length));
    TEST_ASSERT_SIZE(strlen(mathml), length);
    TEST_ASSERT(contains(mathml, "<mfrac>"));
    TEST_ASSERT(contains(mathml, "<mi>α</mi>"));
    TEST_ASSERT(contains(mathml, "<msqrt>"));
    TEST_ASSERT(contains(mathml, "<msub>"));
    TEST_ASSERT(contains(mathml, "<msubsup>"));
    TEST_ASSERT(contains(mathml, "<mo>=</mo>"));
    TEST_ASSERT(contains(mathml, "xmlns=\"http://www.w3.org/1998/Math/MathML\""));
    free(mathml);
    return true;
}

static bool test_mathml_escapes_xml_and_keeps_unknown_commands_visible(void)
{
    char *mathml = NULL;
    size_t length = 0u;
    TEST_ASSERT(render("@document(version:1){@math{a < b & \\unknown}}",
                       &mathml, &length));
    TEST_ASSERT_SIZE(strlen(mathml), length);
    TEST_ASSERT(contains(mathml, "<mo>&lt;</mo>"));
    TEST_ASSERT(contains(mathml, "<mtext>&amp;</mtext>"));
    TEST_ASSERT(contains(mathml, "<mtext>\\unknown</mtext>"));
    TEST_ASSERT(!contains(mathml, "<mo><</mo>"));
    free(mathml);
    return true;
}

static bool test_mathml_preserves_group_delimiters_and_multiple_blocks(void)
{
    char *mathml = NULL;
    size_t length = 0u;
    TEST_ASSERT(render(
        "@document(version:1){@math{(x+1)}@text{between}@math{[y-2]}}",
        &mathml, &length));
    TEST_ASSERT_SIZE(strlen(mathml), length);
    TEST_ASSERT(contains(mathml, "<mo>(</mo>"));
    TEST_ASSERT(contains(mathml, "<mo>[</mo>"));
    TEST_ASSERT(contains(mathml, "<mo>]</mo>"));
    TEST_ASSERT_SIZE(2u, count(mathml, "<mtr>"));
    free(mathml);
    return true;
}

static bool test_mathml_keeps_unmatched_open_delimiters_visible(void)
{
    char *mathml = NULL;
    size_t length = 0u;
    TEST_ASSERT(render("@document(version:1){@math{(x+1}}",
                       &mathml, &length));
    TEST_ASSERT_SIZE(strlen(mathml), length);
    TEST_ASSERT(contains(mathml, "<mo>(</mo>"));
    TEST_ASSERT(contains(mathml, "<mn>1</mn>"));
    free(mathml);
    return true;
}

static bool test_no_math_is_empty_and_bad_arguments_are_rejected(void)
{
    char *mathml = NULL;
    size_t length = 5u;
    TEST_ASSERT(render("@document(version:1){@text{plain}}", &mathml, &length));
    TEST_ASSERT(mathml != NULL);
    TEST_ASSERT_SIZE(0u, length);
    TEST_ASSERT_STRING("", mathml);
    free(mathml);

    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT,
                    trytr_mathml_render(NULL, &mathml, &length));
    return true;
}

int main(void)
{
    TRYTR_RUN(test_mathml_uses_semantic_fraction_root_and_scripts);
    TRYTR_RUN(test_mathml_escapes_xml_and_keeps_unknown_commands_visible);
    TRYTR_RUN(test_mathml_preserves_group_delimiters_and_multiple_blocks);
    TRYTR_RUN(test_mathml_keeps_unmatched_open_delimiters_visible);
    TRYTR_RUN(test_no_math_is_empty_and_bad_arguments_are_rejected);
    return 0;
}
