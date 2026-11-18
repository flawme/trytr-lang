#include "internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void trytr_stream_clear_error(TrytrStream *stream)
{
    trytr_document_destroy(stream->rejected);
    stream->rejected = NULL;
    stream->has_diagnostic = false;
}

TrytrStatus trytr_stream_error(TrytrStream *stream, TrytrStatus status,
                               const char *code, const char *message)
{
    if (stream != NULL) {
        stream->diagnostic = (TrytrDiagnostic){.severity = TRYTR_DIAGNOSTIC_ERROR,
            .code = code, .message = message, .location = {.line = 1u, .column = 1u}};
        stream->has_diagnostic = true;
    }
    return status;
}

TrytrStatus trytr_stream_create(const TrytrStreamOptions *options, TrytrStream **out_stream)
{
    if (out_stream != NULL) *out_stream = NULL;
    if (out_stream == NULL || (options != NULL &&
        (options->canvas_width < 0 || options->canvas_height < 0 ||
         options->canvas_width > TRYTR_DEFAULT_COORDINATE_LIMIT ||
         options->canvas_height > TRYTR_DEFAULT_COORDINATE_LIMIT ||
         (options->canvas_width == 0) != (options->canvas_height == 0))))
        return TRYTR_STATUS_INVALID_ARGUMENT;
    TrytrStream *stream = calloc(1u, sizeof(*stream));
    if (stream == NULL) return TRYTR_STATUS_NO_MEMORY;
    stream->pool = calloc(1u, sizeof(*stream->pool));
    if (stream->pool == NULL) { free(stream); return TRYTR_STATUS_NO_MEMORY; }
    stream->pool->references = 1u;
    char header[128];
    const int length = options == NULL || options->canvas_width == 0
        ? snprintf(header, sizeof(header), "@document(version:1){\n}")
        : snprintf(header, sizeof(header), "@document(version:1,width:%" PRId64
            ",height:%" PRId64 "){\n}", options->canvas_width, options->canvas_height);
    if (length < 0 || (size_t)length >= sizeof(header)) {
        trytr_stream_destroy(stream);
        return TRYTR_STATUS_EXECUTION_FAILED;
    }
    stream->source_length = (size_t)length;
    stream->source = malloc(stream->source_length + 1u);
    if (stream->source == NULL) { trytr_stream_destroy(stream); return TRYTR_STATUS_NO_MEMORY; }
    memcpy(stream->source, header, stream->source_length + 1u);
    TrytrDocument *document = NULL;
    TrytrStatus status = trytr_stream_parse_candidate(stream->source, stream->source_length, &document);
    if (status == TRYTR_STATUS_OK) {
        stream->current = trytr_stream_new_snapshot(stream->pool, document, 0u, stream->source_length);
        if (stream->current == NULL) status = TRYTR_STATUS_NO_MEMORY;
    }
    if (status != TRYTR_STATUS_OK) {
        trytr_document_destroy(document);
        trytr_stream_destroy(stream);
        return status;
    }
    stream->parse_bytes = stream->source_length;
    *out_stream = stream;
    return TRYTR_STATUS_OK;
}

void trytr_stream_destroy(TrytrStream *stream)
{
    if (stream == NULL) return;
    trytr_snapshot_release(stream->current);
    trytr_stream_pool_release(stream->pool);
    trytr_document_destroy(stream->rejected);
    free(stream->source);
    free(stream->pending);
    free(stream);
}

TrytrStatus trytr_stream_begin_append(TrytrStream *stream, uint64_t expected_revision)
{
    if (stream == NULL) return TRYTR_STATUS_INVALID_ARGUMENT;
    trytr_stream_clear_error(stream);
    if (stream->phase != TRYTR_STREAM_IDLE) return trytr_stream_error(stream,
        TRYTR_STATUS_INVALID_ARGUMENT, "TRYTR-E-STREAM-STATE", "begin requires an idle stream");
    if (expected_revision != stream->current->revision) return trytr_stream_error(stream,
        TRYTR_STATUS_REVISION_CONFLICT, "TRYTR-E-REVISION", "append base revision is stale");
    stream->phase = TRYTR_STREAM_APPENDING;
    return TRYTR_STATUS_OK;
}

TrytrStatus trytr_stream_feed(TrytrStream *stream, const char *bytes, size_t length)
{
    if (stream == NULL) return TRYTR_STATUS_INVALID_ARGUMENT;
    trytr_stream_clear_error(stream);
    if (stream->phase != TRYTR_STREAM_APPENDING) return trytr_stream_error(stream,
        TRYTR_STATUS_INVALID_ARGUMENT, "TRYTR-E-STREAM-STATE", "feed requires an open append frame");
    if (bytes == NULL && length != 0u) return trytr_stream_error(stream,
        TRYTR_STATUS_INVALID_ARGUMENT, "TRYTR-E-STREAM-ARG", "nonempty input requires bytes");
    const size_t remaining = stream->source_length < TRYTR_DEFAULT_SOURCE_LIMIT
        ? TRYTR_DEFAULT_SOURCE_LIMIT - stream->source_length - 1u : 0u;
    if (stream->pending_length > remaining || length > remaining - stream->pending_length)
        return trytr_stream_error(stream,
        TRYTR_STATUS_LIMIT_EXCEEDED, "TRYTR-E-LIMIT", "assembled stream source exceeds its byte limit");
    if (length == 0u) return TRYTR_STATUS_OK;
    const size_t required = stream->pending_length + length + 1u;
    if (required > stream->pending_capacity) {
        size_t capacity = stream->pending_capacity == 0u ? 256u : stream->pending_capacity;
        while (capacity < required) capacity *= 2u;
        char *pending = realloc(stream->pending, capacity);
        if (pending == NULL) return trytr_stream_error(stream,
            TRYTR_STATUS_NO_MEMORY, "TRYTR-E-MEMORY", "unable to buffer stream chunks");
        stream->pending = pending;
        stream->pending_capacity = capacity;
    }
    memcpy(stream->pending + stream->pending_length, bytes, length);
    stream->pending_length += length;
    stream->pending[stream->pending_length] = '\0';
    return TRYTR_STATUS_OK;
}

TrytrStatus trytr_stream_cancel_append(TrytrStream *stream)
{
    if (stream == NULL) return TRYTR_STATUS_INVALID_ARGUMENT;
    trytr_stream_clear_error(stream);
    if (stream->phase == TRYTR_STREAM_FINISHED) return trytr_stream_error(stream,
        TRYTR_STATUS_INVALID_ARGUMENT, "TRYTR-E-STREAM-STATE", "stream is already finished");
    free(stream->pending);
    stream->pending = NULL;
    stream->pending_length = stream->pending_capacity = 0u;
    stream->phase = TRYTR_STREAM_IDLE;
    return TRYTR_STATUS_OK;
}

TrytrStatus trytr_stream_finish(TrytrStream *stream)
{
    if (stream == NULL) return TRYTR_STATUS_INVALID_ARGUMENT;
    trytr_stream_clear_error(stream);
    if (stream->phase == TRYTR_STREAM_APPENDING) return trytr_stream_error(stream,
        TRYTR_STATUS_INVALID_ARGUMENT, "TRYTR-E-STREAM-STATE", "commit or cancel the pending frame before finishing");
    stream->phase = TRYTR_STREAM_FINISHED;
    return TRYTR_STATUS_OK;
}

TrytrStreamPhase trytr_stream_phase(const TrytrStream *stream)
{ return stream == NULL ? TRYTR_STREAM_FINISHED : stream->phase; }

uint64_t trytr_stream_revision(const TrytrStream *stream)
{ return stream == NULL ? 0u : stream->current->revision; }

size_t trytr_stream_pending_bytes(const TrytrStream *stream)
{ return stream == NULL ? 0u : stream->pending_length; }

size_t trytr_stream_diagnostic_count(const TrytrStream *stream)
{
    if (stream == NULL) return 0u;
    return (stream->has_diagnostic ? 1u : 0u) + trytr_document_diagnostic_count(stream->rejected);
}

const TrytrDiagnostic *trytr_stream_diagnostic_at(const TrytrStream *stream, size_t index)
{
    if (stream == NULL) return NULL;
    if (stream->has_diagnostic) {
        if (index == 0u) return &stream->diagnostic;
        --index;
    }
    return trytr_document_diagnostic_at(stream->rejected, index);
}
