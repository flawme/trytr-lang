#include "test.h"

#include "svg_render.h"
#include "trytr/document.h"

#include <stdlib.h>

static TrytrStatus render(const char *source, char **svg, size_t *length,
                          TrytrDocument **out_document)
{
    TrytrStatus status = trytr_document_parse(source, strlen(source), out_document);
    if (status != TRYTR_STATUS_OK) return status;
    return trytr_svg_render(*out_document, svg, length);
}

static bool contains(const char *text, const char *needle)
{
    return strstr(text, needle) != NULL;
}

static bool test_svg_renders_all_shapes_and_escapes_text(void)
{
    static const char source[] =
        "@document(version:1){"
        "@heading #title{A & <B>}"
        "@rect #box(10,20,30,40)"
        "@line #edge(1,2,3,4)"
        "@circle #dot(8,9,2)"
        "@ellipse #oval(12,13,4,5)"
        "@path #curve{M 1 2 L 5 6 Z}"
        "}";
    TrytrDocument *document = NULL;
    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, render(source, &svg, &length, &document));
    TEST_ASSERT(svg != NULL);
    TEST_ASSERT_SIZE(strlen(svg), length);
    TEST_ASSERT(contains(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\""));
    TEST_ASSERT(contains(svg, "<title>trytr document</title>"));
    TEST_ASSERT(contains(svg, "<text x=\"16\""));
    TEST_ASSERT(contains(svg, "A &amp; &lt;B&gt;"));
    TEST_ASSERT(contains(svg, "<rect id=\"box\" x=\"10\" y=\"20\" width=\"30\" height=\"40\""));
    TEST_ASSERT(contains(svg, "<line id=\"edge\" x1=\"1\" y1=\"2\" x2=\"3\" y2=\"4\""));
    TEST_ASSERT(contains(svg, "<circle id=\"dot\" cx=\"8\" cy=\"9\" r=\"2\""));
    TEST_ASSERT(contains(svg, "<ellipse id=\"oval\" cx=\"12\" cy=\"13\" rx=\"4\" ry=\"5\""));
    TEST_ASSERT(contains(svg, "<path id=\"curve\" d=\"M 1 2 L 5 6 Z\""));
    TEST_ASSERT(contains(svg, "</svg>\n"));
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_empty_document_has_minimal_valid_svg(void)
{
    static const char source[] = "@document(version:1){}";
    TrytrDocument *document = NULL;
    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, render(source, &svg, &length, &document));
    TEST_ASSERT(contains(svg, "width=\"1\" height=\"1\" viewBox=\"0 0 1 1\""));
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_nested_transforms_apply_in_svg_and_viewbox(void)
{
    static const char source[] =
        "@document(version:1){"
        "@group #outer(10,20){"
        "@group #inner(-2,3){@rect #box(1,2,3,4)}"
        "}"
        "}";
    TrytrDocument *document = NULL;
    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, render(source, &svg, &length, &document));
    TEST_ASSERT(contains(svg, "<g id=\"outer\" transform=\"translate(10 20)\">"));
    TEST_ASSERT(contains(svg, "<g id=\"inner\" transform=\"translate(-2 3)\">"));
    TEST_ASSERT(contains(svg, "<rect id=\"box\" x=\"1\" y=\"2\" width=\"3\" height=\"4\""));
    TEST_ASSERT(contains(svg, "viewBox=\"-7 9 35 36\""));
    TEST_ASSERT(contains(svg, "</g>\n</g>\n</svg>"));
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_invalid_document_and_arguments(void)
{
    static const char invalid_source[] = "@document(version:1){@circle(0,0,-1)}";
    TrytrDocument *document = NULL;
    char *svg = (char *)1;
    size_t length = 99u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(invalid_source, sizeof(invalid_source) - 1u, &document));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                    trytr_svg_render(document, &svg, &length));
    TEST_ASSERT(svg == NULL);
    TEST_ASSERT_SIZE(0u, length);
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_svg_render(NULL, &svg, &length));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_svg_render(document, NULL, &length));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_svg_render(document, &svg, NULL));
    trytr_document_destroy(document);
    return true;
}

static bool test_svg_output_limit(void)
{
    const size_t lines = 300000u;
    const char prefix[] = "@document(version:1){@text{";
    const char suffix[] = "}}";
    const size_t content_length = lines * 2u;
    const size_t source_length = sizeof(prefix) - 1u + content_length + sizeof(suffix) - 1u;
    char *source = malloc(source_length + 1u);
    TEST_ASSERT(source != NULL);
    char *cursor = source;
    memcpy(cursor, prefix, sizeof(prefix) - 1u);
    cursor += sizeof(prefix) - 1u;
    for (size_t index = 0u; index < lines; ++index) {
        *cursor++ = 'x';
        *cursor++ = '\n';
    }
    memcpy(cursor, suffix, sizeof(suffix) - 1u);

    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, source_length, &document));
    char *svg = NULL;
    size_t svg_length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_LIMIT_EXCEEDED,
                    trytr_svg_render(document, &svg, &svg_length));
    TEST_ASSERT(svg == NULL);
    TEST_ASSERT_SIZE(0u, svg_length);
    trytr_document_destroy(document);
    free(source);
    return true;
}

static const TrytrTestCase tests[] = {
    {"shapes and escaped text", test_svg_renders_all_shapes_and_escapes_text},
    {"empty document", test_empty_document_has_minimal_valid_svg},
    {"nested group transforms", test_nested_transforms_apply_in_svg_and_viewbox},
    {"invalid document and arguments", test_invalid_document_and_arguments},
    {"SVG output limit", test_svg_output_limit}
};

TEST_MAIN(tests)
