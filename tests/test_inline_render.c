#include "test.h"
#include "trytr/html.h"
#include "trytr/layout.h"
#include "trytr/math/mathml.h"
#include "svg_render.h"
#include "text_render.h"

#include <stdint.h>
#include <stdlib.h>

static bool parse(const char *source, TrytrDocument **document)
{
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_parse(source, strlen(source), document));
    return true;
}

static bool test_inline_mathml_is_embedded_inside_native_paragraphs(void)
{
    const char source[] = "@document(version:1){"
        "@heading(inline-math:true){A \\(x_i^2\\) heading}"
        "@text(inline-math:true){Before \\(\\frac{1}{\\sqrt{x^2 + 1}}\\) after <safe>.}"
        "@math{y^2}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT(parse(source, &document));
    char *html = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(document, &html, &length));
    const char *paragraph = strstr(html, "<p ");
    TEST_ASSERT(paragraph != NULL);
    const char *math = strstr(paragraph, "display=\"inline\"");
    const char *after = strstr(paragraph, "after &lt;safe&gt;.");
    const char *close = strstr(paragraph, "</p>");
    TEST_ASSERT(math != NULL && after != NULL && close != NULL && math < after && after < close);
    TEST_ASSERT(strstr(paragraph, "<mfrac>") != NULL);
    TEST_ASSERT(strstr(paragraph, "<msqrt>") != NULL);
    TEST_ASSERT(strstr(html, "<msubsup>") != NULL);
    TEST_ASSERT(strstr(html, "display=\"block\"") != NULL);
    TEST_ASSERT(strstr(html, "\\(") == NULL);
    TEST_ASSERT(strstr(html, "\\frac") == NULL);
    TEST_ASSERT(strstr(html, "<img") == NULL);
    TEST_ASSERT_SIZE(strlen(html), length);
    free(html);
    trytr_document_destroy(document);
    return true;
}

static bool test_wrap_never_splits_formula_or_attached_punctuation(void)
{
    const char source[] = "@document(version:1){"
        "@text(font-size:10,width:60,inline-math:true){A \\(x + y\\). B}"
        "@text(font-size:10,width:20,inline-math:true){πpre\\(x + y\\)post.}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT(parse(source, &document));
    TrytrLayout *layout = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_layout_create(document, NULL, &layout));
    const TrytrLayoutBlock *block = trytr_layout_block_at(layout, 0u);
    TEST_ASSERT_SIZE(3u, block->line_count);
    const TrytrLayoutLine *line = trytr_layout_line_at(layout, block->first_line + 1u);
    const TrytrLayoutElement *item = trytr_layout_element_at(layout, block->first_element);
    TEST_ASSERT_SIZE(strlen("\\(x + y\\)."), line->length);
    TEST_ASSERT(memcmp(item->element.text + line->start, "\\(x + y\\).", line->length) == 0);
    TEST_ASSERT_SIZE(1u, trytr_layout_block_at(layout, 1u)->line_count);
    trytr_layout_destroy(layout);
    char *html = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(document, &html, &length));
    TEST_ASSERT(strstr(html, "<span class=\"trytr-line\">A</span>") != NULL);
    TEST_ASSERT(strstr(html, "</math>.</span>") != NULL);
    TEST_ASSERT(strstr(html, "<span class=\"trytr-line\">B</span>") != NULL);
    TEST_ASSERT(strstr(html, "πpre<math") != NULL);
    TEST_ASSERT(strstr(html, "</math>post.") != NULL);
    free(html);
    trytr_document_destroy(document);
    return true;
}

static bool test_escaped_delimiters_unknown_commands_and_multiple_formulas(void)
{
    const char source[] = "@document(version:1){"
        "@text(inline-math:true){Use \\\\(literal\\\\) and $5; \\(a\\) then \\(\\unknown{b}\\).}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT(parse(source, &document));
    char *html = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(document, &html, &length));
    TEST_ASSERT(strstr(html, "Use \\(literal\\) and $5;") != NULL);
    TEST_ASSERT(strstr(html, "Use \\\\(literal") == NULL);
    TEST_ASSERT(strstr(html, "<mi>a</mi>") != NULL);
    TEST_ASSERT(strstr(html, "\\unknown") != NULL);
    const char *first = strstr(html, "display=\"inline\"");
    TEST_ASSERT(first != NULL);
    const char *second = strstr(first + 1, "display=\"inline\"");
    TEST_ASSERT(second != NULL);
    TEST_ASSERT(strstr(second + 1, "display=\"inline\"") == NULL);
    free(html);
    trytr_document_destroy(document);
    return true;
}

static bool test_legacy_markup_and_fallback_renderers_keep_source(void)
{
    const char legacy[] = "@document(version:1){@text{Cost $5, \\(x\\) is literal.}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT(parse(legacy, &document));
    char *output = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(document, &output, &length));
    TEST_ASSERT(strstr(output, "\\(x\\)") != NULL);
    TEST_ASSERT(strstr(output, "<math ") == NULL);
    free(output);
    trytr_document_destroy(document);
    const char enabled[] = "@document(version:1){@text(inline-math:true){Let \\(x_i\\) be 2.}}";
    TEST_ASSERT(parse(enabled, &document));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_text_render(document, &output, &length));
    TEST_ASSERT_STRING("Let \\(x_i\\) be 2.\n", output);
    free(output);
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_svg_render(document, &output, &length));
    TEST_ASSERT(strstr(output, "\\(x_i\\)") != NULL);
    free(output);
    trytr_document_destroy(document);
    return true;
}

static bool capture_element(const TrytrDocumentElement *element, void *data)
{ *(TrytrDocumentElement *)data = *element; return true; }

static bool test_mathml_run_api_checks_bounds_and_owns_output(void)
{
    const char source[] = "@document(version:1){@text(inline-math:true){π \\(x < y\\).}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT(parse(source, &document));
    TrytrDocumentElement element = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_execute(document, capture_element, &element));
    const TrytrInlineRun *math = element.inline_runs->next;
    char *output = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_mathml_render_inline_run(&element, math, &output, &length));
    TEST_ASSERT(strstr(output, "display=\"inline\"") != NULL);
    TEST_ASSERT(strstr(output, "<mo>&lt;</mo>") != NULL);
    TEST_ASSERT(strstr(output, "π") == NULL);
    TEST_ASSERT_SIZE(strlen(output), length);
    free(output);
    TrytrInlineRun bad = *math;
    bad.source_offset = SIZE_MAX;
    output = (char *)1;
    length = 99u;
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT,
        trytr_mathml_render_inline_run(&element, &bad, &output, &length));
    TEST_ASSERT(output == NULL);
    TEST_ASSERT_SIZE(0u, length);
    bad = *math;
    bad.content_length = SIZE_MAX;
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT,
        trytr_mathml_render_inline_run(&element, &bad, &output, &length));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT,
        trytr_mathml_render_inline_run(&element, element.inline_runs, &output, &length));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT,
        trytr_mathml_render_inline_run(NULL, math, &output, &length));
    trytr_document_destroy(document);
    return true;
}

static const TrytrTestCase tests[] = {
    {"native paragraphs and inline MathML", test_inline_mathml_is_embedded_inside_native_paragraphs},
    {"atomic formula wrapping", test_wrap_never_splits_formula_or_attached_punctuation},
    {"escapes unknown commands and multiple formulas", test_escaped_delimiters_unknown_commands_and_multiple_formulas},
    {"legacy and fallback rendering", test_legacy_markup_and_fallback_renderers_keep_source},
    {"run API bounds and ownership", test_mathml_run_api_checks_bounds_and_owns_output}
};
TEST_MAIN(tests)
