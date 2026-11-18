#include "internal.h"

#include <stdlib.h>

TrytrSnapshot *trytr_stream_new_snapshot(TrytrSnapshotPool *pool,
    TrytrDocument *document, uint64_t revision, size_t source_bytes)
{
    TrytrSnapshot *snapshot = calloc(1u, sizeof(*snapshot));
    if (snapshot == NULL) return NULL;
    *snapshot = (TrytrSnapshot){.references = 1u, .revision = revision,
        .source_bytes = source_bytes, .document = document, .pool = pool};
    ++pool->references;
    ++pool->live;
    return snapshot;
}

void trytr_stream_pool_release(TrytrSnapshotPool *pool)
{
    if (pool != NULL && --pool->references == 0u) free(pool);
}

TrytrSnapshot *trytr_stream_acquire_snapshot(const TrytrStream *stream)
{ return stream == NULL ? NULL : trytr_snapshot_retain(stream->current); }

TrytrSnapshot *trytr_snapshot_retain(TrytrSnapshot *snapshot)
{
    if (snapshot == NULL || snapshot->references == SIZE_MAX) return NULL;
    ++snapshot->references;
    return snapshot;
}

void trytr_snapshot_release(TrytrSnapshot *snapshot)
{
    if (snapshot == NULL || --snapshot->references != 0u) return;
    TrytrSnapshotPool *pool = snapshot->pool;
    trytr_document_destroy(snapshot->document);
    free(snapshot);
    --pool->live;
    trytr_stream_pool_release(pool);
}

TrytrDocument *trytr_snapshot_document(const TrytrSnapshot *snapshot)
{ return snapshot == NULL ? NULL : snapshot->document; }

uint64_t trytr_snapshot_revision(const TrytrSnapshot *snapshot)
{ return snapshot == NULL ? 0u : snapshot->revision; }

size_t trytr_snapshot_source_bytes(const TrytrSnapshot *snapshot)
{ return snapshot == NULL ? 0u : snapshot->source_bytes; }
