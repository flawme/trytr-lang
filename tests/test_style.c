#include "test.h"

#include "trytr/document.h"

typedef struct CapturedElements {
    TrytrDocumentElement elements[8];
    size_t count;
} CapturedElements;

static bool capture_element(const TrytrDocumentElement *element, void *user_data)
{
    CapturedElements *captured = user_data;
    if (captured->count >= sizeof(captured->elements) / sizeof(captured->elements[0])) {
        return false;
    }
    captured->elements[captured->count++] = *element;
    return true;
}

static bool has_diagnostic(const TrytrDocument *document, const char *code)
{
    const size_t count = trytr_document_diagnostic_count(document);
    for (size_t index = 0u; index < count; ++index) {
        const TrytrDiagnostic *diagnostic = trytr_document_diagnostic_at(document, index);
        if (diagnostic != NULL && strcmp(diagnostic->code, code) == 0) return true;
    }
    return false;
}

static bool test_colors_reach_public_elements(void)
{
    static const char source[] =
        "@document(version:1){"
        "@rect #card{x:1 y:2 width:30 height:40 "
        "fill:rgb(230,57,70) stroke:rgb(20,40,60) stroke-width:4}"
        "@line #edge{x1:0 y1:0 x2:10 y2:10 stroke:rgb(6*2,17+17,112/2) stroke-width:16*2}"
        "@path #curve(stroke:rgb(1,2,3),stroke-width:3){M 0 0 L 10 10}"
        "@polygon #tri(fill:rgb(4,5,6),stroke:rgb(7,8,9),stroke-width:4){0 0 10 0 5 10}"
        "@circle #plain{cx:1 cy:2 radius:3}"
        "}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    CapturedElements captured = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_execute(document, capture_element, &captured));
    TEST_ASSERT_SIZE(5u, captured.count);
    TEST_ASSERT(captured.elements[0].has_fill);
    TEST_ASSERT_INT(0xE63946, captured.elements[0].fill_rgb);
    TEST_ASSERT(captured.elements[0].has_stroke);
    TEST_ASSERT_INT(0x14283C, captured.elements[0].stroke_rgb);
    TEST_ASSERT(captured.elements[0].has_stroke_width);
    TEST_ASSERT_INT(4, captured.elements[0].stroke_width);
    TEST_ASSERT(!captured.elements[1].has_fill);
    TEST_ASSERT(captured.elements[1].has_stroke);
    TEST_ASSERT_INT(0x0C2238, captured.elements[1].stroke_rgb);
    TEST_ASSERT(captured.elements[1].has_stroke_width);
    TEST_ASSERT_INT(32, captured.elements[1].stroke_width);
    TEST_ASSERT(captured.elements[2].has_stroke);
    TEST_ASSERT_INT(0x010203, captured.elements[2].stroke_rgb);
    TEST_ASSERT_INT(3, captured.elements[2].stroke_width);
    TEST_ASSERT(captured.elements[3].has_fill);
    TEST_ASSERT_INT(0x040506, captured.elements[3].fill_rgb);
    TEST_ASSERT(captured.elements[3].has_stroke);
    TEST_ASSERT_INT(0x070809, captured.elements[3].stroke_rgb);
    TEST_ASSERT_INT(4, captured.elements[3].stroke_width);
    TEST_ASSERT(!captured.elements[4].has_fill);
    TEST_ASSERT(!captured.elements[4].has_stroke);
    trytr_document_destroy(document);
    return true;
}

static bool test_rgb_channels_must_be_in_range(void)
{
    static const char source[] =
        "@document(version:1){"
        "@rect{x:0 y:0 width:1 height:1 fill:rgb(256,0,0)}"
        "}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    TEST_ASSERT(document != NULL);
    TEST_ASSERT(has_diagnostic(document, "TRYTR-E-COLOR"));
    trytr_document_destroy(document);
    return true;
}

static bool test_fill_is_rejected_for_lines(void)
{
    static const char source[] =
        "@document(version:1){"
        "@line{x1:0 y1:0 x2:1 y2:1 fill:rgb(1,2,3)}"
        "}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    CapturedElements captured = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                    trytr_document_execute(document, capture_element, &captured));
    TEST_ASSERT(has_diagnostic(document, "TRYTR-E-PROPERTY"));
    trytr_document_destroy(document);
    return true;
}

static bool test_duplicate_color_property_is_rejected(void)
{
    static const char source[] =
        "@document(version:1){"
        "@circle{cx:0 cy:0 radius:5 fill:rgb(1,2,3) fill:rgb(4,5,6)}"
        "}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                    trytr_document_validate(document));
    TEST_ASSERT(has_diagnostic(document, "TRYTR-E-PROPERTY"));
    trytr_document_destroy(document);
    return true;
}

static bool test_stroke_width_is_bounded(void)
{
    static const char source[] =
        "@document(version:1){"
        "@circle{cx:0 cy:0 radius:5 stroke-width:33}"
        "}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                    trytr_document_validate(document));
    TEST_ASSERT(has_diagnostic(document, "TRYTR-E-STROKE-WIDTH"));
    trytr_document_destroy(document);
    return true;
}

static bool test_text_styles_reach_public_elements(void)
{
    static const char source[] =
        "@document(version:1){"
        "@heading #title(font-size:32,color:rgb(12,34,56),align:center,width:120){Title}"
        "}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    CapturedElements captured = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_execute(document, capture_element, &captured));
    TEST_ASSERT_SIZE(1u, captured.count);
    TEST_ASSERT(captured.elements[0].has_font_size);
    TEST_ASSERT_INT(32, captured.elements[0].font_size);
    TEST_ASSERT(captured.elements[0].has_text_color);
    TEST_ASSERT_INT(0x0C2238, captured.elements[0].text_color_rgb);
    TEST_ASSERT(captured.elements[0].has_text_alignment);
    TEST_ASSERT_INT(TRYTR_TEXT_ALIGN_CENTER, captured.elements[0].text_alignment);
    TEST_ASSERT(captured.elements[0].has_text_width);
    TEST_ASSERT_INT(120, captured.elements[0].text_width);
    trytr_document_destroy(document);
    return true;
}

static bool test_text_font_size_must_be_bounded(void)
{
    static const char source[] =
        "@document(version:1){@text(font-size:257){Too large}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                    trytr_document_validate(document));
    TEST_ASSERT(has_diagnostic(document, "TRYTR-E-FONT-SIZE"));
    trytr_document_destroy(document);
    return true;
}

static bool test_text_alignment_uses_supported_values(void)
{
    static const char source[] =
        "@document(version:1){@text(align:diagonal){Bad alignment}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    TEST_ASSERT(has_diagnostic(document, "TRYTR-E-TEXT-ALIGN"));
    trytr_document_destroy(document);
    return true;
}

static const TrytrTestCase tests[] = {
    {"colors in public element views", test_colors_reach_public_elements},
    {"RGB channel bounds", test_rgb_channels_must_be_in_range},
    {"line fill rejection", test_fill_is_rejected_for_lines},
    {"duplicate color property", test_duplicate_color_property_is_rejected},
    {"stroke width bounds", test_stroke_width_is_bounded},
    {"text style API", test_text_styles_reach_public_elements},
    {"text font size bounds", test_text_font_size_must_be_bounded},
    {"text alignment values", test_text_alignment_uses_supported_values}
};

TEST_MAIN(tests)
