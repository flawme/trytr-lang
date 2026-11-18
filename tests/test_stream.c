#include "test.h"
#include "trytr/stream.h"
#include "trytr/html.h"

#include <stdlib.h>

static bool has_code(const TrytrStream *stream, const char *code)
{
    for (size_t index = 0u; index < trytr_stream_diagnostic_count(stream); ++index) {
        if (strcmp(trytr_stream_diagnostic_at(stream, index)->code, code) == 0) return true;
    }
    return false;
}

static bool append(TrytrStream *stream, const char *source)
{
    bool changed = false;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, trytr_stream_revision(stream)));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, source, strlen(source)));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_commit_append(stream, &changed));
    TEST_ASSERT(changed);
    return true;
}

static bool test_incomplete_frames_preserve_previous_snapshot_and_can_resume(void)
{
    TrytrStream *stream = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_create(NULL, &stream));
    TEST_ASSERT(append(stream, "@heading #title{Visible title}"));
    TrytrSnapshot *old = trytr_stream_acquire_snapshot(stream);
    TEST_ASSERT(old != NULL);
    const char *partial = "@text(inline-math:true){Let \\(x^";
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, 1u));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, partial, strlen(partial)));
    TEST_ASSERT_SIZE(0u, trytr_stream_diagnostic_count(stream));
    TEST_ASSERT_INT(1, trytr_stream_revision(stream));
    bool changed = true;
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT, trytr_stream_commit_append(stream, &changed));
    TEST_ASSERT(!changed);
    TEST_ASSERT(trytr_stream_diagnostic_count(stream) > 0u);
    TEST_ASSERT_INT(TRYTR_STREAM_APPENDING, trytr_stream_phase(stream));
    TEST_ASSERT_INT(1, trytr_stream_revision(stream));
    TEST_ASSERT_SIZE(strlen(partial), trytr_stream_pending_bytes(stream));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, "2\\) be positive.}", strlen("2\\) be positive.}")));
    TEST_ASSERT_SIZE(0u, trytr_stream_diagnostic_count(stream));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_commit_append(stream, &changed));
    TEST_ASSERT(changed);
    TEST_ASSERT_INT(2, trytr_stream_revision(stream));
    TEST_ASSERT_SIZE(0u, trytr_stream_pending_bytes(stream));
    char *html = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(trytr_snapshot_document(old), &html, &length));
    TEST_ASSERT(strstr(html, "Visible title") != NULL);
    TEST_ASSERT(strstr(html, "be positive") == NULL);
    free(html);
    TrytrSnapshot *current = trytr_stream_acquire_snapshot(stream);
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(trytr_snapshot_document(current), &html, &length));
    TEST_ASSERT(strstr(html, "display=\"inline\"") != NULL);
    TEST_ASSERT(strstr(html, "be positive") != NULL);
    free(html);
    trytr_stream_destroy(stream);
    TEST_ASSERT_INT(1, trytr_snapshot_revision(old));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(trytr_snapshot_document(old), &html, &length));
    free(html);
    trytr_snapshot_release(current);
    trytr_snapshot_release(old);
    return true;
}

static bool test_rejected_append_frames_never_replace_valid_content(void)
{
    static const char *const invalid[] = {
        "@text #same{Duplicate}", "@circle(0,0,-1)", "@path{M (1 / 0) 0}",
        "@text(inline-math:true){bad \\(\\)}", "@rect(#unknown,0,2,2)",
        "@param #late(min:0,max:2,value:1,step:1){Late}",
        "@document(version:1){}", "} // try to escape root", "} /* root escape */",
        "@group(0,0){@text{Unfinished group}", "/* unfinished comment"
    };
    TrytrStream *stream = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_create(NULL, &stream));
    TEST_ASSERT(append(stream, "@text #same{Good content}"));
    for (size_t index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, 1u));
        TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, invalid[index], strlen(invalid[index])));
        bool changed = true;
        TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT, trytr_stream_commit_append(stream, &changed));
        TEST_ASSERT(!changed);
        TEST_ASSERT_INT(1, trytr_stream_revision(stream));
        TEST_ASSERT(trytr_stream_diagnostic_count(stream) > 0u);
        TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_cancel_append(stream));
        TEST_ASSERT_SIZE(0u, trytr_stream_pending_bytes(stream));
    }
    TEST_ASSERT(append(stream, "@text #next{Recovered}"));
    TEST_ASSERT_INT(2, trytr_stream_revision(stream));
    trytr_stream_destroy(stream);
    return true;
}

static bool test_revisions_empty_frames_and_finish_follow_lifecycle(void)
{
    TrytrStream *stream = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_create(NULL, &stream));
    TEST_ASSERT_INT(TRYTR_STREAM_IDLE, trytr_stream_phase(stream));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, 0u));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, NULL, 0u));
    bool changed = true;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_commit_append(stream, &changed));
    TEST_ASSERT(!changed);
    TEST_ASSERT_INT(0, trytr_stream_revision(stream));
    TEST_ASSERT(append(stream, "@text{One}"));
    TEST_ASSERT_INT(TRYTR_STATUS_REVISION_CONFLICT, trytr_stream_begin_append(stream, 0u));
    TEST_ASSERT(has_code(stream, "TRYTR-E-REVISION"));
    TEST_ASSERT_INT(TRYTR_STREAM_IDLE, trytr_stream_phase(stream));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, 1u));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_finish(stream));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_begin_append(stream, 1u));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_cancel_append(stream));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_finish(stream));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_finish(stream));
    TEST_ASSERT_INT(TRYTR_STREAM_FINISHED, trytr_stream_phase(stream));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_begin_append(stream, 1u));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_feed(stream, "x", 1u));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_commit_append(stream, &changed));
    TEST_ASSERT(!changed);
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_cancel_append(stream));
    TrytrSnapshot *snapshot = trytr_stream_acquire_snapshot(stream);
    TEST_ASSERT(snapshot != NULL);
    trytr_stream_destroy(stream);
    trytr_snapshot_release(snapshot);
    return true;
}

static bool test_api_arguments_and_fixed_canvas_options(void)
{
    const TrytrStreamOptions invalid[] = {{.canvas_width = -1}, {.canvas_width = 100},
        {.canvas_height = 50}, {.canvas_width = 1000001, .canvas_height = 20}};
    TrytrStream *stream = NULL;
    for (size_t index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        stream = (TrytrStream *)1;
        TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_create(&invalid[index], &stream));
        TEST_ASSERT(stream == NULL);
    }
    const TrytrStreamOptions options = {.canvas_width = 100, .canvas_height = 60};
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_create(&options, &stream));
    TrytrSnapshot *snapshot = trytr_stream_acquire_snapshot(stream);
    int64_t width = 0, height = 0;
    TEST_ASSERT(trytr_document_get_canvas_size(trytr_snapshot_document(snapshot), &width, &height));
    TEST_ASSERT_INT(100, width);
    TEST_ASSERT_INT(60, height);
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_feed(stream, "x", 1u));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, 0u));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_feed(stream, NULL, 1u));
    TEST_ASSERT_SIZE(0u, trytr_stream_pending_bytes(stream));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_commit_append(stream, NULL));
    TEST_ASSERT_INT(TRYTR_STREAM_APPENDING, trytr_stream_phase(stream));
    trytr_snapshot_release(snapshot);
    trytr_stream_destroy(stream);
    bool changed = true;
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_create(NULL, NULL));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_begin_append(NULL, 0u));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_feed(NULL, NULL, 0u));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_commit_append(NULL, &changed));
    TEST_ASSERT(!changed);
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_cancel_append(NULL));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_finish(NULL));
    TEST_ASSERT(trytr_stream_acquire_snapshot(NULL) == NULL);
    TEST_ASSERT(trytr_snapshot_retain(NULL) == NULL);
    TEST_ASSERT(trytr_snapshot_document(NULL) == NULL);
    TEST_ASSERT_SIZE(0u, trytr_stream_diagnostic_count(NULL));
    TEST_ASSERT(trytr_stream_diagnostic_at(NULL, 0u) == NULL);
    TEST_ASSERT_STRING("document revision conflict", trytr_status_string(TRYTR_STATUS_REVISION_CONFLICT));
    trytr_snapshot_release(NULL);
    trytr_stream_destroy(NULL);
    return true;
}

static const TrytrTestCase tests[] = {
    {"incomplete frames resume and preserve snapshots", test_incomplete_frames_preserve_previous_snapshot_and_can_resume},
    {"rejected frames preserve valid content", test_rejected_append_frames_never_replace_valid_content},
    {"revisions and lifecycle", test_revisions_empty_frames_and_finish_follow_lifecycle},
    {"arguments and fixed canvases", test_api_arguments_and_fixed_canvas_options}
};
TEST_MAIN(tests)
