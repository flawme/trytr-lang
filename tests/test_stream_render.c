#include "test.h"
#include "trytr/stream.h"
#include "trytr/html.h"
#include "trytr/layout.h"

#include <stdlib.h>

static bool test_every_byte_boundary_produces_identical_native_output(void)
{
    const char frame[] =
        "@param #n(min:1,max:4,value:2,step:1){N}"
        "@heading{π title} /* structural comment */"
        "@text(inline-math:true){Before \\(x_{#n}^2\\); // literal /* text */}"
        "@group(3,4){@rect{x:1 y:2 width:3 height:4 fill:rgb(1,2,3)}@path{M #n 0 L (#n + 1) 4}}"
        "@polygon{0,0 5,0 3,6}@math{\\frac{1}{2}}"
        "@plot(48,0,100,50)(x-min:0,x-max:2,samples:3,markers:false){y=#n*x}"
        "// trailing comment";
    const char prefix[] = "@document(version:1){\n";
    const size_t source_length = sizeof(prefix) - 1u + sizeof(frame) - 1u + 2u;
    char *source = malloc(source_length + 1u);
    TEST_ASSERT(source != NULL);
    memcpy(source, prefix, sizeof(prefix) - 1u);
    memcpy(source + sizeof(prefix) - 1u, frame, sizeof(frame) - 1u);
    memcpy(source + source_length - 2u, "\n}", 3u);
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_parse(source, source_length, &document));
    char *expected = NULL;
    size_t expected_length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(document, &expected, &expected_length));
    for (size_t split = 0u; split < sizeof(frame); ++split) {
        TrytrStream *stream = NULL;
        TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_create(NULL, &stream));
        TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, 0u));
        TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, frame, split));
        TEST_ASSERT_INT(0, trytr_stream_revision(stream));
        TEST_ASSERT_SIZE(0u, trytr_stream_diagnostic_count(stream));
        TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, frame + split, sizeof(frame) - 1u - split));
        bool changed = false;
        TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_commit_append(stream, &changed));
        TEST_ASSERT(changed);
        TrytrSnapshot *snapshot = trytr_stream_acquire_snapshot(stream);
        TEST_ASSERT_SIZE(source_length, trytr_snapshot_source_bytes(snapshot));
        char *actual = NULL;
        size_t actual_length = 0u;
        TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(trytr_snapshot_document(snapshot), &actual, &actual_length));
        TEST_ASSERT_SIZE(expected_length, actual_length);
        TEST_ASSERT_STRING(expected, actual);
        free(actual);
        trytr_snapshot_release(snapshot);
        trytr_stream_destroy(stream);
    }
    free(expected);
    free(source);
    trytr_document_destroy(document);
    return true;
}

static bool test_split_utf8_can_resume_after_a_rejected_early_commit(void)
{
    TrytrStream *stream = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_create(NULL, &stream));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, 0u));
    const char prefix[] = "@text{\xCF";
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, prefix, sizeof(prefix) - 1u));
    bool changed = true;
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT, trytr_stream_commit_append(stream, &changed));
    TEST_ASSERT(!changed);
    TEST_ASSERT_INT(0, trytr_stream_revision(stream));
    const char suffix[] = "\x80}";
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, suffix, sizeof(suffix) - 1u));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_commit_append(stream, &changed));
    TEST_ASSERT(changed);
    TrytrSnapshot *snapshot = trytr_stream_acquire_snapshot(stream);
    TrytrLayout *layout = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_layout_create(trytr_snapshot_document(snapshot), NULL, &layout));
    TEST_ASSERT_SIZE(1u, trytr_layout_block_count(layout));
    TEST_ASSERT_STRING("π", trytr_layout_element_at(layout, 0u)->element.text);
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, 1u));
    const char *next = "@text{A later paragraph}";
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, next, strlen(next)));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_commit_append(stream, &changed));
    trytr_stream_destroy(stream);
    TEST_ASSERT_SIZE(1u, trytr_layout_block_count(layout));
    TEST_ASSERT_STRING("π", trytr_layout_element_at(layout, 0u)->element.text);
    trytr_layout_destroy(layout);
    trytr_snapshot_release(snapshot);
    return true;
}

static const TrytrTestCase tests[] = {
    {"all byte splits match complete-document rendering", test_every_byte_boundary_produces_identical_native_output},
    {"split UTF-8 recovery and old layout lifetime", test_split_utf8_can_resume_after_a_rejected_early_commit}
};
TEST_MAIN(tests)
