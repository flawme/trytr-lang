#include "test.h"
#include "trytr/stream.h"
#include "trytr/html.h"

#include <stdlib.h>

static bool capture_plot(const TrytrDocumentElement *element, void *data)
{
    if (element->kind == TRYTR_ELEMENT_PLOT) *(const char **)data = element->text;
    return true;
}

static bool append(TrytrStream *stream, const char *frame)
{
    bool changed = false;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, trytr_stream_revision(stream)));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_feed(stream, frame, strlen(frame)));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_commit_append(stream, &changed));
    TEST_ASSERT(changed);
    return true;
}

static bool test_updates_rebuild_all_bound_content_and_preserve_old_snapshots(void)
{
    TrytrStream *stream = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_create(NULL, &stream));
    TEST_ASSERT(append(stream,
        "@param #n(min:0,max:10,value:2,step:2){Amount}"
        "@param #m(min:0,max:10,value:4,step:2){Other}"
        "@text(inline-math:true){Value \\(x=#n\\)}"
        "@path{M #n 0 L (#n + 1) 5}"
        "@plot(0,0,100,100)(x-min:0,x-max:2,samples:3){y=#n*x}"));
    TrytrSnapshot *old = trytr_stream_acquire_snapshot(stream);
    TrytrParameterOverride change = {.id = "n", .value = 6};
    bool changed = false;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_set_parameters(stream, 1u, &change, 1u, &changed));
    TEST_ASSERT(changed);
    TrytrSnapshot *updated = trytr_stream_acquire_snapshot(stream);
    TEST_ASSERT_SIZE(trytr_snapshot_source_bytes(old), trytr_snapshot_source_bytes(updated));
    TEST_ASSERT_INT(2, trytr_document_parameter_at(trytr_snapshot_document(old), 0u)->value);
    TEST_ASSERT_INT(6, trytr_document_parameter_at(trytr_snapshot_document(updated), 0u)->value);
    TEST_ASSERT_INT(4, trytr_document_parameter_at(trytr_snapshot_document(updated), 1u)->value);
    char *old_html = NULL, *new_html = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(trytr_snapshot_document(old), &old_html, &length));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(trytr_snapshot_document(updated), &new_html, &length));
    TEST_ASSERT(strstr(old_html, "<mn>2</mn>") != NULL);
    TEST_ASSERT(strstr(new_html, "<mn>6</mn>") != NULL);
    TEST_ASSERT(strstr(old_html, "M 2 0 L 3 5") != NULL);
    TEST_ASSERT(strstr(new_html, "M 6 0 L 7 5") != NULL);
    const char *old_plot = NULL, *new_plot = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_execute(trytr_snapshot_document(old), capture_plot, &old_plot));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_execute(trytr_snapshot_document(updated), capture_plot, &new_plot));
    TEST_ASSERT(old_plot != NULL && new_plot != NULL && strcmp(old_plot, new_plot) != 0);
    free(old_html);
    free(new_html);
    TEST_ASSERT(append(stream, "@text{Later value #n}"));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_finish(stream));
    change.value = 8;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_set_parameters(stream, 3u, &change, 1u, &changed));
    TEST_ASSERT(changed);
    TEST_ASSERT_INT(TRYTR_STREAM_FINISHED, trytr_stream_phase(stream));
    TrytrSnapshot *final = trytr_stream_acquire_snapshot(stream);
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_html_render(trytr_snapshot_document(final), &new_html, &length));
    TEST_ASSERT(strstr(new_html, "Later value 8") != NULL);
    free(new_html);
    trytr_stream_destroy(stream);
    TEST_ASSERT_INT(6, trytr_document_parameter_at(trytr_snapshot_document(updated), 0u)->value);
    trytr_snapshot_release(old);
    trytr_snapshot_release(updated);
    trytr_snapshot_release(final);
    return true;
}

static bool test_failed_batches_are_atomic_and_noops_do_not_advance_revisions(void)
{
    TrytrStream *stream = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_create(NULL, &stream));
    TEST_ASSERT(append(stream,
        "@param #n(min:0,max:10,value:2,step:2){N}"
        "@param #m(min:0,max:10,value:4,step:2){M}@text{#n #m}"));
    const TrytrParameterOverride bad[][2] = {
        {{"n",6},{"m",11}}, {{"n",6},{"m",3}}, {{"n",6},{"absent",2}},
        {{"n",6},{"n",8}}, {{"n",6},{NULL,2}}, {{"n",6},{"",2}},
        {{"n",6},{"m",INT64_MIN}}, {{"n",6},{"m",INT64_MAX}}
    };
    for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        bool changed = true;
        TEST_ASSERT(trytr_stream_set_parameters(stream, 1u, bad[i], 2u, &changed) != TRYTR_STATUS_OK);
        TEST_ASSERT(!changed);
        TEST_ASSERT_INT(1, trytr_stream_revision(stream));
        TEST_ASSERT(trytr_stream_diagnostic_count(stream) != 0u);
        TrytrSnapshot *snapshot = trytr_stream_acquire_snapshot(stream);
        TEST_ASSERT_INT(2, trytr_document_parameter_at(trytr_snapshot_document(snapshot), 0u)->value);
        trytr_snapshot_release(snapshot);
    }
    bool changed = true;
    TrytrParameterOverride same = {"n",2};
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_set_parameters(stream, 1u, &same, 1u, &changed));
    TEST_ASSERT(!changed);
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_set_parameters(stream, 1u, NULL, 0u, &changed));
    TEST_ASSERT(!changed);
    TEST_ASSERT_INT(TRYTR_STATUS_REVISION_CONFLICT, trytr_stream_set_parameters(stream, 0u, NULL, 0u, &changed));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_set_parameters(stream, 1u, NULL, 1u, &changed));
    TEST_ASSERT_INT(TRYTR_STATUS_LIMIT_EXCEEDED, trytr_stream_set_parameters(stream, 1u, &same, SIZE_MAX, &changed));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_set_parameters(stream, 1u, &same, 1u, NULL));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_begin_append(stream, 1u));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_set_parameters(stream, 1u, &same, 1u, &changed));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_cancel_append(stream));
    const TrytrParameterOverride both[] = {{"n",6},{"m",8}};
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_set_parameters(stream, 1u, both, 2u, &changed));
    TEST_ASSERT(changed);
    TrytrSnapshot *batch = trytr_stream_acquire_snapshot(stream);
    TEST_ASSERT_INT(6, trytr_document_parameter_at(trytr_snapshot_document(batch), 0u)->value);
    TEST_ASSERT_INT(8, trytr_document_parameter_at(trytr_snapshot_document(batch), 1u)->value);
    trytr_snapshot_release(batch);
    trytr_stream_destroy(stream);
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_stream_set_parameters(NULL, 0u, NULL, 0u, &changed));
    TEST_ASSERT(!changed);
    return true;
}

static bool test_geometry_failure_does_not_publish_changed_parameters(void)
{
    TrytrStream *stream = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_create(NULL, &stream));
    TEST_ASSERT(append(stream,
        "@param #n(min:0,max:10,value:2,step:1){N}@path{M (10 / #n) 0}"));
    TrytrParameterOverride change = {"n",0};
    bool changed = true;
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT, trytr_stream_set_parameters(stream, 1u, &change, 1u, &changed));
    TEST_ASSERT(!changed);
    TEST_ASSERT_INT(1, trytr_stream_revision(stream));
    change.value = 5;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_set_parameters(stream, 1u, &change, 1u, &changed));
    TEST_ASSERT(changed);
    trytr_stream_destroy(stream);
    return true;
}

static bool test_parameter_retention_pressure_is_retryable(void)
{
    TrytrStream *stream = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_create(NULL, &stream));
    TEST_ASSERT(append(stream, "@param #n(min:0,max:10,value:0,step:1){N}@text{#n}"));
    TrytrSnapshot *held[8] = {0};
    held[0] = trytr_stream_acquire_snapshot(stream);
    bool changed = false;
    for (size_t i = 1u; i < 8u; ++i) {
        const TrytrParameterOverride update = {"n", (int64_t)i};
        TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_set_parameters(stream, i, &update, 1u, &changed));
        held[i] = trytr_stream_acquire_snapshot(stream);
    }
    const TrytrParameterOverride same = {"n",7}, next = {"n",8};
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_set_parameters(stream, 8u, &same, 1u, &changed));
    TEST_ASSERT(!changed);
    TEST_ASSERT_INT(TRYTR_STATUS_LIMIT_EXCEEDED, trytr_stream_set_parameters(stream, 8u, &next, 1u, &changed));
    TEST_ASSERT(!changed);
    TEST_ASSERT_INT(8, trytr_stream_revision(stream));
    trytr_snapshot_release(held[0]);
    held[0] = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_stream_set_parameters(stream, 8u, &next, 1u, &changed));
    TEST_ASSERT(changed);
    trytr_stream_destroy(stream);
    for (size_t i = 0u; i < 8u; ++i) trytr_snapshot_release(held[i]);
    return true;
}

static const TrytrTestCase tests[] = {
    {"parameters persist across appends and finish", test_updates_rebuild_all_bound_content_and_preserve_old_snapshots},
    {"invalid batches, noops, states, and conflicts", test_failed_batches_are_atomic_and_noops_do_not_advance_revisions},
    {"geometry failure preserves parameters", test_geometry_failure_does_not_publish_changed_parameters},
    {"parameter retention backpressure", test_parameter_retention_pressure_is_retryable}
};
TEST_MAIN(tests)
