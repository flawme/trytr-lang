#include "test.h"

#include "text_render.h"
#include "trytr/document.h"

#include <stdlib.h>

static bool contains(const char *text, const char *needle)
{
    return strstr(text, needle) != NULL;
}

static bool contains_non_ascii(const char *text)
{
    for (const unsigned char *cursor = (const unsigned char *)text;
         *cursor != 0u; ++cursor) {
        if (*cursor >= 0x80u) return true;
    }
    return false;
}

static bool test_text_renderer_emits_graph_as_inline_characters(void)
{
    static const char source[] =
        "@document(version:1,width:100,height:50){"
        "@heading #title{Revenue}\n"
        "@line #x-axis(0,40,100,40)"
        "@line #y-axis(0,0,0,40)"
        "@path #series{M 5 35 L 15 20 L 50 20 C 60 12 75 18 95 5}"
        "}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    char *text = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_text_render(document, &text, &length));
    TEST_ASSERT(contains(text, "# Revenue\n"));
    TEST_ASSERT(contains_non_ascii(text));
    TEST_ASSERT(contains(text, "\xf0\x9f\xac"));
    TEST_ASSERT(!contains(text, "╭") && !contains(text, "╯"));
    TEST_ASSERT(!contains(text, "<svg"));
    TEST_ASSERT_SIZE(strlen(text), length);
    free(text);
    trytr_document_destroy(document);
    return true;
}

static bool test_text_renderer_keeps_text_only_documents_as_text(void)
{
    static const char source[] =
        "@document(version:1){@heading{A note}@text{First line\nSecond line}"
        "@math{x_{i+1} = y^2}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    char *text = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_text_render(document, &text, &length));
    TEST_ASSERT_STRING("# A note\nFirst line\nSecond line\n= x_{i+1} = y^2\n", text);
    TEST_ASSERT_SIZE(strlen(text), length);
    free(text);
    trytr_document_destroy(document);
    return true;
}

static bool test_text_renderer_draws_primitives_and_groups(void)
{
    static const char source[] =
        "@document(version:1,width:100,height:100){"
        "@rect(5,5,20,20)@roundrect(35,5,20,20,4)"
        "@circle(20,65,10)@ellipse(65,65,18,10)"
        "@polygon{5, 40 25, 40 15, 55}"
        "@group(5,5){@line(60,35,90,55)}"
        "}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    char *text = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_text_render(document, &text, &length));
    TEST_ASSERT(contains(text, "+"));
    TEST_ASSERT(contains(text, "*"));
    TEST_ASSERT(contains(text, "-"));
    TEST_ASSERT_SIZE(strlen(text), length);
    free(text);
    trytr_document_destroy(document);
    return true;
}

static bool test_text_renderer_maps_subcells_to_block_glyphs(void)
{
    static const char source[] =
        "@document(version:1,width:100,height:100){"
        "@line #vertical(0,0,0,100)@line #horizontal(0,50,100,50)}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    char *text = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_text_render(document, &text, &length));
    TEST_ASSERT(contains(text, "\xe2\x96\x8c")); /* full left half */
    TEST_ASSERT(contains(text, "\xf0\x9f\xac\x82")); /* upper third */
    TEST_ASSERT_SIZE(strlen(text), length);
    free(text);
    trytr_document_destroy(document);
    return true;
}

static const TrytrTestCase tests[] = {
    {"inline text graph", test_text_renderer_emits_graph_as_inline_characters},
    {"text-only output", test_text_renderer_keeps_text_only_documents_as_text},
    {"shape and group output", test_text_renderer_draws_primitives_and_groups},
    {"subcell block glyph mapping", test_text_renderer_maps_subcells_to_block_glyphs}
};

TEST_MAIN(tests)
