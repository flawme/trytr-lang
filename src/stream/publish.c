#include "internal.h"
#include "../document_internal.h"

#include <stdlib.h>
#include <string.h>

static bool preflight(const TrytrDocumentElement *element, void *data)
{ (void)element; (void)data; return true; }

TrytrStatus trytr_stream_parse_candidate(const char *source, size_t length,
                                        TrytrDocument **out_document)
{
    TrytrStatus status = trytr_document_parse(source, length, out_document);
    if (status == TRYTR_STATUS_OK) status = trytr_document_execute(*out_document, preflight, NULL);
    return status;
}

TrytrStatus trytr_stream_commit_append(TrytrStream *stream, bool *out_changed)
{
    if (out_changed != NULL) *out_changed = false;
    if (stream == NULL) return TRYTR_STATUS_INVALID_ARGUMENT;
    trytr_stream_clear_error(stream);
    if (out_changed == NULL) return trytr_stream_error(stream,
        TRYTR_STATUS_INVALID_ARGUMENT, "TRYTR-E-STREAM-ARG", "commit requires an output change flag");
    if (stream->phase != TRYTR_STREAM_APPENDING) return trytr_stream_error(stream,
        TRYTR_STATUS_INVALID_ARGUMENT, "TRYTR-E-STREAM-STATE", "commit requires an open append frame");
    if (stream->pending_length == 0u) return trytr_stream_cancel_append(stream);
    const size_t released = stream->current->references == 1u ? 1u : 0u;
    if (stream->pool->live - released >= TRYTR_STREAM_LIVE_LIMIT) return trytr_stream_error(stream,
        TRYTR_STATUS_LIMIT_EXCEEDED, "TRYTR-E-STREAM-SNAPSHOTS", "release an old snapshot before publishing another");
    if (stream->current->revision == UINT64_MAX) return trytr_stream_error(stream,
        TRYTR_STATUS_LIMIT_EXCEEDED, "TRYTR-E-REVISION", "stream revision counter is exhausted");
    const size_t length = stream->source_length + stream->pending_length + 1u;
    if (length > TRYTR_STREAM_PARSE_LIMIT - stream->parse_bytes) return trytr_stream_error(stream,
        TRYTR_STATUS_LIMIT_EXCEEDED, "TRYTR-E-STREAM-WORK", "stream candidate parsing exceeds its cumulative byte budget");
    char *candidate = malloc(length + 1u);
    if (candidate == NULL) return trytr_stream_error(stream,
        TRYTR_STATUS_NO_MEMORY, "TRYTR-E-MEMORY", "unable to assemble append candidate");
    const size_t body_end = stream->source_length - 1u;
    memcpy(candidate, stream->source, body_end);
    memcpy(candidate + body_end, stream->pending, stream->pending_length);
    memcpy(candidate + body_end + stream->pending_length, "\n}", 3u);
    stream->parse_bytes += length;
    TrytrDocument *document = NULL;
    TrytrStatus status = trytr_stream_parse_candidate(candidate, length, &document);
    if (status == TRYTR_STATUS_OK && trytr_document_body_end(document) != length - 1u) {
        status = trytr_stream_error(stream, TRYTR_STATUS_INVALID_DOCUMENT,
            "TRYTR-E-STREAM-FRAME", "append frames must stay inside the document body");
    }
    if (status != TRYTR_STATUS_OK) {
        stream->rejected = document;
        if (!stream->has_diagnostic && trytr_document_diagnostic_count(document) == 0u)
            (void)trytr_stream_error(stream, status, "TRYTR-E-STREAM-CANDIDATE",
                                     "append candidate could not be compiled within document limits");
        free(candidate);
        return status;
    }
    TrytrSnapshot *snapshot = trytr_stream_new_snapshot(stream->pool, document,
        stream->current->revision + 1u, length);
    if (snapshot == NULL) {
        trytr_document_destroy(document);
        free(candidate);
        return trytr_stream_error(stream, TRYTR_STATUS_NO_MEMORY,
            "TRYTR-E-MEMORY", "unable to allocate the committed snapshot");
    }
    TrytrSnapshot *previous = stream->current;
    stream->current = snapshot;
    free(stream->source);
    stream->source = candidate;
    stream->source_length = length;
    (void)trytr_stream_cancel_append(stream);
    trytr_snapshot_release(previous);
    *out_changed = true;
    return TRYTR_STATUS_OK;
}
