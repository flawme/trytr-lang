#include "test.h"

#include "trytr/document.h"

typedef struct RoundrectCapture {
    TrytrDocumentElement elements[4];
    size_t count;
} RoundrectCapture;

static bool capture_element(const TrytrDocumentElement *element, void *user_data)
{
    RoundrectCapture *capture = user_data;
    if (capture->count < sizeof(capture->elements) / sizeof(capture->elements[0])) {
        capture->elements[capture->count] = *element;
    }
    ++capture->count;
    return true;
}

static bool test_positional_and_named_roundrect_geometry(void)
{
    static const char source[] =
        "@document(version:1){"
        "@roundrect #positional(10,20,80,40,20)"
        "@roundrect #named{x:1 y:2 width:30 height:20 radius:10}"
        "}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));

    RoundrectCapture capture = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_execute(document, capture_element, &capture));
    TEST_ASSERT_SIZE(2u, capture.count);

    TEST_ASSERT_INT(TRYTR_ELEMENT_ROUNDRECT, capture.elements[0].kind);
    TEST_ASSERT_STRING("positional", capture.elements[0].id);
    TEST_ASSERT_INT(10, capture.elements[0].x);
    TEST_ASSERT_INT(20, capture.elements[0].y);
    TEST_ASSERT_INT(80, capture.elements[0].width);
    TEST_ASSERT_INT(40, capture.elements[0].height);
    TEST_ASSERT_INT(20, capture.elements[0].radius);

    TEST_ASSERT_INT(TRYTR_ELEMENT_ROUNDRECT, capture.elements[1].kind);
    TEST_ASSERT_STRING("named", capture.elements[1].id);
    TEST_ASSERT_INT(1, capture.elements[1].x);
    TEST_ASSERT_INT(2, capture.elements[1].y);
    TEST_ASSERT_INT(30, capture.elements[1].width);
    TEST_ASSERT_INT(20, capture.elements[1].height);
    TEST_ASSERT_INT(10, capture.elements[1].radius);
    trytr_document_destroy(document);
    return true;
}

static bool test_radius_must_fit_both_dimensions(void)
{
    static const char *const invalid_sources[] = {
        "@document(version:1){@roundrect(0,0,20,20,11)}",
        "@document(version:1){@roundrect(0,0,20,10,6)}",
        "@document(version:1){@roundrect(0,0,20,20,-1)}"
    };
    for (size_t index = 0u;
         index < sizeof(invalid_sources) / sizeof(invalid_sources[0]);
         ++index) {
        TrytrDocument *document = NULL;
        const char *source = invalid_sources[index];
        TEST_ASSERT_INT(TRYTR_STATUS_OK,
                        trytr_document_parse(source, strlen(source), &document));
        RoundrectCapture capture = {0};
        TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                        trytr_document_execute(document, capture_element, &capture));
        TEST_ASSERT_SIZE(0u, capture.count);
        TEST_ASSERT(trytr_document_diagnostic_count(document) > 0u);
        trytr_document_destroy(document);
    }
    return true;
}

static bool test_zero_radius_is_valid(void)
{
    static const char source[] = "@document(version:1){@roundrect(0,0,0,0,0)}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    RoundrectCapture capture = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_execute(document, capture_element, &capture));
    TEST_ASSERT_SIZE(1u, capture.count);
    TEST_ASSERT_INT(0, capture.elements[0].radius);
    trytr_document_destroy(document);
    return true;
}

static const TrytrTestCase tests[] = {
    {"positional and named geometry", test_positional_and_named_roundrect_geometry},
    {"radius dimension validation", test_radius_must_fit_both_dimensions},
    {"zero corner radius", test_zero_radius_is_valid}
};

TEST_MAIN(tests)
