#include "test.h"
#include "trytr/html.h"
#include "trytr/math/mathml.h"
#include "svg_render.h"
#include "text_render.h"

#include <stdlib.h>

static bool test_native_equations_and_canvases_keep_reading_order(void)
{
    static const char source[] = "@document(version:1){"
        "@heading #title{A document}@text #before{Before equation}"
        "@math #equation{\\frac{1}{\\sqrt{x^2 + 1}}}"
        "@line #first(0,0,10,10)@circle #dot(5,5,2)"
        "@text #between{Between drawings}@path #last{M 0 0 L 10 10}"
        "@text #after{After drawings}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_parse(source, sizeof(source) - 1u, &document));
    char *html = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(document, &html, &length));
    TEST_ASSERT(strstr(html, "<!doctype html>") != NULL);
    TEST_ASSERT(strstr(html, "<mfrac>") != NULL);
    TEST_ASSERT(strstr(html, "<msqrt>") != NULL);
    TEST_ASSERT(strstr(html, "<msup>") != NULL);
    TEST_ASSERT(strstr(html, "\\frac") == NULL);
    TEST_ASSERT(strstr(html, "<script") == NULL);
    TEST_ASSERT(strstr(html, "<img") == NULL);
    const char *needles[] = {"id=\"before\"", "id=\"equation\"", "id=\"first\"",
                             "id=\"between\"", "id=\"last\"", "id=\"after\""};
    const char *previous = html;
    for (size_t index = 0u; index < sizeof(needles) / sizeof(needles[0]); ++index) {
        const char *position = strstr(html, needles[index]);
        TEST_ASSERT(position != NULL && position > previous);
        previous = position;
    }
    TEST_ASSERT_SIZE(strlen(html), length);
    free(html);

    char *text = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_text_render(document, &text, &length));
    const char *middle = strstr(text, "Between drawings");
    const char *after = strstr(text, "After drawings");
    TEST_ASSERT(middle != NULL && after != NULL && after > middle);
    const char *drawing = strstr(text, "\xf0\x9f\xac");
    TEST_ASSERT(drawing != NULL && drawing < middle);
    TEST_ASSERT(strstr(middle, "\xf0\x9f\xac") != NULL);
    free(text);
    trytr_document_destroy(document);
    return true;
}

static bool test_html_escapes_source_and_preserves_text_style(void)
{
    static const char source[] = "@document(version:1){"
        "@text #caption(font-size:20,width:100,color:rgb(12,34,56),align:right)"
        "{<script>\"A&B\"</script>}@math{a < b & c}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_parse(source, sizeof(source) - 1u, &document));
    char *html = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(document, &html, &length));
    TEST_ASSERT(strstr(html, "&lt;script&gt;&quot;A&amp;B&quot;&lt;/script&gt;") != NULL);
    TEST_ASSERT(strstr(html, "<script>") == NULL);
    TEST_ASSERT(strstr(html, "font-size:20px;line-height:26px;") != NULL);
    TEST_ASSERT(strstr(html, "color:#0c2238;") != NULL);
    TEST_ASSERT(strstr(html, "text-align:right;") != NULL);
    TEST_ASSERT(strstr(html, "&lt;") != NULL && strstr(html, "&amp;") != NULL);
    free(html);
    trytr_document_destroy(document);
    return true;
}

static bool test_translated_canvas_fragments_and_parameter_overrides(void)
{
    static const char source[] = "@document(version:1,width:100,height:60){"
        "@param #n(min:1,max:10,value:2,step:1){N}@math{y = #n}"
        "@group(10,20){@group(-5,3){@path #curve{M #n 0 L (#n * 2) 10}}}}";
    TrytrDocument *document = NULL;
    const TrytrParameterOverride override = {.id = "n", .value = 7};
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_parse_with_parameters(
        source, sizeof(source) - 1u, &override, 1u, &document));
    char *html = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(document, &html, &length));
    TEST_ASSERT(strstr(html, "<mn>7</mn>") != NULL);
    TEST_ASSERT(strstr(html, "translate(5 23)") != NULL);
    TEST_ASSERT(strstr(html, "d=\"M 7 0 L 14 10\"") != NULL);
    TEST_ASSERT(strstr(html, "viewBox=\"0 0 100 60\"") != NULL);
    TEST_ASSERT(strstr(html, "#n") == NULL);
    free(html);
    trytr_document_destroy(document);
    return true;
}

static bool capture_one_math(const TrytrDocumentElement *element, void *user_data)
{
    if (element->kind != TRYTR_ELEMENT_MATH) return true;
    *(TrytrDocumentElement *)user_data = *element;
    return true;
}

static bool test_element_mathml_and_failures_have_clear_ownership(void)
{
    TrytrDocument *document = NULL;
    const char source[] = "@document(version:1){@math{\\frac{1}{2}}}";
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_parse(source, sizeof(source) - 1u, &document));
    TrytrDocumentElement element = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_execute(document, capture_one_math, &element));
    char *output = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_mathml_render_element(&element, &output, &length));
    TEST_ASSERT(strstr(output, "display=\"block\"><mrow>") != NULL);
    TEST_ASSERT(strstr(output, "<mfrac>") != NULL);
    TEST_ASSERT(strstr(output, "<mtable>") == NULL);
    TEST_ASSERT_SIZE(strlen(output), length);
    free(output);
    trytr_document_destroy(document);
    output = (char *)1;
    length = 99u;
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_html_render(NULL, &output, &length));
    TEST_ASSERT(output == NULL);
    TEST_ASSERT_SIZE(0u, length);
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_mathml_render_element(NULL, &output, &length));
    TEST_ASSERT(output == NULL);
    const char invalid[] = "@document(version:1){@circle(0,0,-1)}";
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_parse(invalid, sizeof(invalid) - 1u, &document));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT, trytr_html_render(document, &output, &length));
    TEST_ASSERT(output == NULL);
    TEST_ASSERT_SIZE(0u, length);
    trytr_document_destroy(document);
    return true;
}

static const TrytrTestCase tests[] = {
    {"native math and reading order", test_native_equations_and_canvases_keep_reading_order},
    {"escaped source and text style", test_html_escapes_source_and_preserves_text_style},
    {"canvas transforms and parameter overrides", test_translated_canvas_fragments_and_parameter_overrides},
    {"standalone equation ownership and failure", test_element_mathml_and_failures_have_clear_ownership}
};
TEST_MAIN(tests)
