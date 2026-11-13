#include "test.h"

#include "trytr/document.h"

#include <string.h>

typedef struct VisitorCapture {
    TrytrDocumentElement elements[12];
    size_t count;
    size_t stop_after;
} VisitorCapture;

static bool capture_element(const TrytrDocumentElement *element, void *user_data)
{
    VisitorCapture *capture = user_data;
    if (capture->count < sizeof(capture->elements) / sizeof(capture->elements[0])) {
        capture->elements[capture->count] = *element;
    }
    ++capture->count;
    return capture->stop_after == 0u || capture->count < capture->stop_after;
}

static TrytrStatus parse_document(const char *source, TrytrDocument **out_document)
{
    return trytr_document_parse(source, strlen(source), out_document);
}

static bool test_visitor_receives_all_elements_in_source_order(void)
{
    static const char source[] =
        "@document(version:1){"
        "@heading #head{Title}"
        "@text #copy{A & B}"
        "@math #equation{a+b}"
        "@rect #box(1,2,3,4)"
        "@line #edge(5,6,7,8)"
        "@circle #dot(9,10,11)"
        "@ellipse #oval(13,14,15,16)"
        "@polygon #triangle{1, 2, 3, 4, -5, 6}"
        "@path #curve{M 1 2 C 3 4 5 6 7 8}"
        "}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, parse_document(source, &document));
    VisitorCapture capture = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_execute(document, capture_element, &capture));
    TEST_ASSERT_SIZE(9u, capture.count);

    static const TrytrElementKind kinds[] = {
        TRYTR_ELEMENT_HEADING, TRYTR_ELEMENT_TEXT, TRYTR_ELEMENT_MATH,
        TRYTR_ELEMENT_RECT, TRYTR_ELEMENT_LINE, TRYTR_ELEMENT_CIRCLE,
        TRYTR_ELEMENT_ELLIPSE, TRYTR_ELEMENT_POLYGON, TRYTR_ELEMENT_PATH
    };
    static const char *const ids[] = {
        "head", "copy", "equation", "box", "edge", "dot", "oval", "triangle", "curve"
    };
    for (size_t index = 0u; index < 9u; ++index) {
        TEST_ASSERT_INT(kinds[index], capture.elements[index].kind);
        TEST_ASSERT_SIZE(index, capture.elements[index].element_index);
        TEST_ASSERT_STRING(ids[index], capture.elements[index].id);
    }
    TEST_ASSERT_STRING("Title", capture.elements[0].text);
    TEST_ASSERT_SIZE(5u, capture.elements[0].text_length);
    TEST_ASSERT_INT(1, capture.elements[3].x);
    TEST_ASSERT_INT(2, capture.elements[3].y);
    TEST_ASSERT_INT(3, capture.elements[3].width);
    TEST_ASSERT_INT(4, capture.elements[3].height);
    TEST_ASSERT_INT(5, capture.elements[4].x);
    TEST_ASSERT_INT(6, capture.elements[4].y);
    TEST_ASSERT_INT(7, capture.elements[4].x2);
    TEST_ASSERT_INT(8, capture.elements[4].y2);
    TEST_ASSERT_INT(9, capture.elements[5].x);
    TEST_ASSERT_INT(10, capture.elements[5].y);
    TEST_ASSERT_INT(11, capture.elements[5].radius);
    TEST_ASSERT_INT(13, capture.elements[6].x);
    TEST_ASSERT_INT(14, capture.elements[6].y);
    TEST_ASSERT_INT(15, capture.elements[6].radius_x);
    TEST_ASSERT_INT(16, capture.elements[6].radius_y);
    TEST_ASSERT_STRING("1, 2, 3, 4, -5, 6", capture.elements[7].text);
    TEST_ASSERT_INT(-5, capture.elements[7].min_x);
    TEST_ASSERT_INT(2, capture.elements[7].min_y);
    TEST_ASSERT_INT(3, capture.elements[7].max_x);
    TEST_ASSERT_INT(6, capture.elements[7].max_y);
    TEST_ASSERT_STRING("M 1 2 C 3 4 5 6 7 8", capture.elements[8].text);
    TEST_ASSERT_INT(1, capture.elements[8].min_x);
    TEST_ASSERT_INT(2, capture.elements[8].min_y);
    TEST_ASSERT_INT(7, capture.elements[8].max_x);
    TEST_ASSERT_INT(8, capture.elements[8].max_y);
    trytr_document_destroy(document);
    return true;
}

static bool test_callback_stop_is_reported_and_document_can_run_again(void)
{
    static const char source[] = "@document(version:1){@text{one}@text{two}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, parse_document(source, &document));
    VisitorCapture capture = {.stop_after = 1u};
    TEST_ASSERT_INT(TRYTR_STATUS_EXECUTION_FAILED,
                    trytr_document_execute(document, capture_element, &capture));
    TEST_ASSERT_SIZE(1u, capture.count);

    capture = (VisitorCapture){0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_execute(document, capture_element, &capture));
    TEST_ASSERT_SIZE(2u, capture.count);
    trytr_document_destroy(document);
    return true;
}

static bool test_invalid_geometry_prevents_vm_callbacks(void)
{
    static const char source[] = "@document(version:1){@circle(0,0,-1)}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, parse_document(source, &document));
    VisitorCapture capture = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                    trytr_document_execute(document, capture_element, &capture));
    TEST_ASSERT_SIZE(0u, capture.count);
    TEST_ASSERT(trytr_document_diagnostic_count(document) > 0u);
    trytr_document_destroy(document);
    return true;
}

static bool test_execution_argument_validation(void)
{
    static const char source[] = "@document(version:1){}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, parse_document(source, &document));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT,
                    trytr_document_execute(document, NULL, NULL));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT,
                    trytr_document_execute(NULL, capture_element, NULL));
    trytr_document_destroy(document);
    return true;
}

static bool test_group_begin_end_events_preserve_nesting(void)
{
    static const char source[] =
        "@document(version:1){"
        "@group #outer(10,-3){"
        "@rect #box(1,2,3,4)"
        "@group #inner(2,4){@circle #dot(5,6,1)}"
        "}"
        "@text #tail{end}"
        "}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, parse_document(source, &document));
    VisitorCapture capture = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_execute(document, capture_element, &capture));
    TEST_ASSERT_SIZE(7u, capture.count);
    TEST_ASSERT_INT(TRYTR_ELEMENT_GROUP_BEGIN, capture.elements[0].kind);
    TEST_ASSERT_STRING("outer", capture.elements[0].id);
    TEST_ASSERT_INT(10, capture.elements[0].x);
    TEST_ASSERT_INT(-3, capture.elements[0].y);
    TEST_ASSERT_INT(TRYTR_ELEMENT_RECT, capture.elements[1].kind);
    TEST_ASSERT_INT(TRYTR_ELEMENT_GROUP_BEGIN, capture.elements[2].kind);
    TEST_ASSERT_STRING("inner", capture.elements[2].id);
    TEST_ASSERT_INT(TRYTR_ELEMENT_CIRCLE, capture.elements[3].kind);
    TEST_ASSERT_INT(TRYTR_ELEMENT_GROUP_END, capture.elements[4].kind);
    TEST_ASSERT_STRING("inner", capture.elements[4].id);
    TEST_ASSERT_INT(TRYTR_ELEMENT_GROUP_END, capture.elements[5].kind);
    TEST_ASSERT_STRING("outer", capture.elements[5].id);
    TEST_ASSERT_INT(TRYTR_ELEMENT_TEXT, capture.elements[6].kind);
    trytr_document_destroy(document);
    return true;
}

static const TrytrTestCase tests[] = {
    {"visitor source order and element data", test_visitor_receives_all_elements_in_source_order},
    {"callback stop and subsequent execution", test_callback_stop_is_reported_and_document_can_run_again},
    {"invalid geometry blocks execution", test_invalid_geometry_prevents_vm_callbacks},
    {"execution argument validation", test_execution_argument_validation},
    {"group begin and end events", test_group_begin_end_events_preserve_nesting}
};

TEST_MAIN(tests)
