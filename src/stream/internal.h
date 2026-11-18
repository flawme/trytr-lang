#ifndef TRYTR_STREAM_INTERNAL_H
#define TRYTR_STREAM_INTERNAL_H

#include "trytr/stream.h"
#include "../limits.h"

#define TRYTR_STREAM_LIVE_LIMIT 8u
#define TRYTR_STREAM_PARSE_LIMIT (64u * TRYTR_DEFAULT_SOURCE_LIMIT)

typedef struct TrytrSnapshotPool {
    size_t references;
    size_t live;
} TrytrSnapshotPool;

struct TrytrSnapshot {
    size_t references;
    uint64_t revision;
    size_t source_bytes;
    TrytrDocument *document;
    TrytrSnapshotPool *pool;
};

struct TrytrStream {
    TrytrStreamPhase phase;
    TrytrSnapshotPool *pool;
    TrytrSnapshot *current;
    char *source;
    size_t source_length;
    char *pending;
    size_t pending_length, pending_capacity;
    size_t parse_bytes;
    TrytrDocument *rejected;
    TrytrDiagnostic diagnostic;
    bool has_diagnostic;
};

TrytrSnapshot *trytr_stream_new_snapshot(TrytrSnapshotPool *pool,
    TrytrDocument *document, uint64_t revision, size_t source_bytes);
void trytr_stream_pool_release(TrytrSnapshotPool *pool);
void trytr_stream_clear_error(TrytrStream *stream);
TrytrStatus trytr_stream_error(TrytrStream *stream, TrytrStatus status,
                               const char *code, const char *message);
TrytrStatus trytr_stream_parse_candidate(const char *source, size_t length,
                                        TrytrDocument **out_document);
size_t trytr_stream_current_parameters(const TrytrStream *stream,
                                       TrytrParameterOverride *overrides);
TrytrStatus trytr_stream_commit_source(TrytrStream *stream, const char *source,
    size_t length, const TrytrParameterOverride *overrides, size_t count, bool *out_changed);

#endif
