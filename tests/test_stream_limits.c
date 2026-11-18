#include "test.h"
#include "trytr/stream.h"
#include "trytr/html.h"
#include "limits.h"

#include <stdint.h>
#include <stdlib.h>

static bool has_code(const TrytrStream *stream, const char *code)
{
    for (size_t index = 0u; index < trytr_stream_diagnostic_count(stream); ++index)
        if (strcmp(trytr_stream_diagnostic_at(stream, index)->code, code) == 0) return true;
    return false;
}

static bool test_exact_source_limit_and_oversized_feeds_accept_no_extra_bytes(void)
{
    TrytrStream *stream = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_create(NULL, &stream));
    TrytrSnapshot *initial = trytr_stream_acquire_snapshot(stream);
    const size_t length = TRYTR_DEFAULT_SOURCE_LIMIT - trytr_snapshot_source_bytes(initial) - 1u;
    char *spaces = malloc(length);
    TEST_ASSERT(spaces != NULL);
    memset(spaces, ' ', length);
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, 0u));
    TEST_ASSERT_INT(TRYTR_STATUS_LIMIT_EXCEEDED, trytr_stream_feed(stream, "x", SIZE_MAX));
    TEST_ASSERT_SIZE(0u, trytr_stream_pending_bytes(stream));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, spaces, length));
    TEST_ASSERT_INT(TRYTR_STATUS_LIMIT_EXCEEDED, trytr_stream_feed(stream, "x", 1u));
    TEST_ASSERT_SIZE(length, trytr_stream_pending_bytes(stream));
    bool changed = false;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_commit_append(stream, &changed));
    TEST_ASSERT(changed);
    TrytrSnapshot *full = trytr_stream_acquire_snapshot(stream);
    TEST_ASSERT_SIZE(TRYTR_DEFAULT_SOURCE_LIMIT, trytr_snapshot_source_bytes(full));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, 1u));
    TEST_ASSERT_INT(TRYTR_STATUS_LIMIT_EXCEEDED, trytr_stream_feed(stream, "x", 1u));
    TEST_ASSERT_SIZE(0u, trytr_stream_pending_bytes(stream));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_commit_append(stream, &changed));
    TEST_ASSERT(!changed);
    TEST_ASSERT_INT(1, trytr_stream_revision(stream));
    free(spaces);
    trytr_snapshot_release(initial);
    trytr_snapshot_release(full);
    trytr_stream_destroy(stream);
    return true;
}

static bool test_snapshot_pressure_is_retryable_and_old_handles_survive_destruction(void)
{
    TrytrStream *stream = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_create(NULL, &stream));
    TrytrSnapshot *held[8] = {0};
    held[0] = trytr_stream_acquire_snapshot(stream);
    const char *frame = "@text{Retained content}";
    for (uint64_t revision = 1u; revision < 8u; ++revision) {
        TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, revision - 1u));
        TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, frame, strlen(frame)));
        bool changed = false;
        TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_commit_append(stream, &changed));
        TEST_ASSERT(changed);
        held[revision] = trytr_stream_acquire_snapshot(stream);
        TEST_ASSERT(held[revision] != NULL);
    }
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, 7u));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, frame, strlen(frame)));
    bool changed = true;
    TEST_ASSERT_INT(TRYTR_STATUS_LIMIT_EXCEEDED, trytr_stream_commit_append(stream, &changed));
    TEST_ASSERT(!changed);
    TEST_ASSERT(has_code(stream, "TRYTR-E-STREAM-SNAPSHOTS"));
    TEST_ASSERT_INT(7, trytr_stream_revision(stream));
    TEST_ASSERT_SIZE(strlen(frame), trytr_stream_pending_bytes(stream));
    TrytrSnapshot *same = trytr_snapshot_retain(held[7]);
    TEST_ASSERT(same == held[7]);
    trytr_snapshot_release(held[0]);
    held[0] = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_commit_append(stream, &changed));
    TEST_ASSERT(changed);
    TEST_ASSERT_INT(8, trytr_stream_revision(stream));
    TEST_ASSERT_INT(7, trytr_snapshot_revision(same));
    trytr_stream_destroy(stream);
    char *html = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(trytr_snapshot_document(same), &html, &length));
    TEST_ASSERT(strstr(html, "Retained content") != NULL);
    free(html);
    trytr_snapshot_release(same);
    for (size_t index = 0u; index < 8u; ++index) trytr_snapshot_release(held[index]);
    return true;
}

static bool test_repeated_bad_candidates_have_a_bounded_total_parse_budget(void)
{
    TrytrStream *stream = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_create(NULL, &stream));
    const size_t length = 256u * 1024u;
    char *bad = malloc(length);
    TEST_ASSERT(bad != NULL);
    memset(bad, ' ', length);
    bad[0] = (char)0xFF; /* Invalid UTF-8 near the start; retries must still be charged. */
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, 0u));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, bad, length));
    bool limited = false;
    for (size_t attempt = 0u; attempt < 1024u; ++attempt) {
        bool changed = true;
        const TrytrStatus status = trytr_stream_commit_append(stream, &changed);
        TEST_ASSERT(!changed);
        TEST_ASSERT_INT(0, trytr_stream_revision(stream));
        if (status == TRYTR_STATUS_LIMIT_EXCEEDED) {
            TEST_ASSERT(has_code(stream, "TRYTR-E-STREAM-WORK"));
            limited = true;
            break;
        }
        TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT, status);
        TEST_ASSERT(has_code(stream, "TRYTR-E-UTF8"));
    }
    TEST_ASSERT(limited);
    TEST_ASSERT_SIZE(length, trytr_stream_pending_bytes(stream));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_cancel_append(stream));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_finish(stream));
    free(bad);
    trytr_stream_destroy(stream);
    return true;
}

static bool test_parseable_frames_must_pass_execution_limits_before_publication(void)
{
    const size_t capacity = 4u * (TRYTR_DEFAULT_PATH_SEGMENT_LIMIT * 6u + 8u);
    char *frame = malloc(capacity);
    TEST_ASSERT(frame != NULL);
    size_t length = 0u;
    for (size_t path = 0u; path < 4u; ++path) {
        memcpy(frame + length, "@path{M 0 0", 11u);
        length += 11u;
        for (size_t segment = 1u; segment < TRYTR_DEFAULT_PATH_SEGMENT_LIMIT; ++segment) {
            memcpy(frame + length, " L 0 0", 6u);
            length += 6u;
        }
        frame[length++] = '}';
    }
    TrytrStream *stream = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_create(NULL, &stream));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, 0u));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, frame, length));
    bool changed = true;
    TEST_ASSERT_INT(TRYTR_STATUS_LIMIT_EXCEEDED, trytr_stream_commit_append(stream, &changed));
    TEST_ASSERT(!changed);
    TEST_ASSERT(has_code(stream, "TRYTR-E-STREAM-CANDIDATE"));
    TEST_ASSERT_INT(0, trytr_stream_revision(stream));
    TEST_ASSERT_SIZE(length, trytr_stream_pending_bytes(stream));
    free(frame);
    /* Destruction must also reclaim the rejected candidate and pending bytes. */
    trytr_stream_destroy(stream);
    return true;
}

static const TrytrTestCase tests[] = {
    {"exact source limit and atomic feed rejection", test_exact_source_limit_and_oversized_feeds_accept_no_extra_bytes},
    {"snapshot retention pressure and lifetime", test_snapshot_pressure_is_retryable_and_old_handles_survive_destruction},
    {"cumulative parse budget", test_repeated_bad_candidates_have_a_bounded_total_parse_budget},
    {"execution budget before publication", test_parseable_frames_must_pass_execution_limits_before_publication}
};
TEST_MAIN(tests)
