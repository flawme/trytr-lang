#ifndef TRYTR_STREAM_H
#define TRYTR_STREAM_H

#include "trytr/document.h"

typedef struct TrytrStream TrytrStream;
typedef struct TrytrSnapshot TrytrSnapshot;

typedef enum TrytrStreamPhase {
    TRYTR_STREAM_IDLE,
    TRYTR_STREAM_APPENDING,
    TRYTR_STREAM_FINISHED
} TrytrStreamPhase;

typedef struct TrytrStreamOptions {
    /* Both zero for fitted canvases, or both 1..1,000,000 for fixed canvases. */
    int64_t canvas_width;
    int64_t canvas_height;
} TrytrStreamOptions;

/* Create revision 0, an empty @document(version:1). All APIs are single-threaded. */
TrytrStatus trytr_stream_create(const TrytrStreamOptions *options, TrytrStream **out_stream);
void trytr_stream_destroy(TrytrStream *stream);

/* Begin an append frame against the current revision; stale revisions conflict.
 * Frames contain document-body elements, not an @document wrapper. */
TrytrStatus trytr_stream_begin_append(TrytrStream *stream, uint64_t expected_revision);
/* Buffer arbitrary chunks, including partial UTF-8/math, without publishing.
 * Failure accepts no bytes. NULL is valid only when length is zero. */
TrytrStatus trytr_stream_feed(TrytrStream *stream, const char *bytes, size_t length);
/* Parse, validate, and compile the full candidate before atomic publication.
 * On error the current snapshot and pending frame survive. Feed more bytes to
 * complete it, or cancel it. Empty frames end without changing the revision.
 * Revision numbers reflect accepted source, including comment-only frames. */
TrytrStatus trytr_stream_commit_append(TrytrStream *stream, bool *out_changed);
TrytrStatus trytr_stream_cancel_append(TrytrStream *stream);
/* Finish only from IDLE; repeated finish calls are allowed. */
TrytrStatus trytr_stream_finish(TrytrStream *stream);

TrytrStreamPhase trytr_stream_phase(const TrytrStream *stream);
uint64_t trytr_stream_revision(const TrytrStream *stream);
size_t trytr_stream_pending_bytes(const TrytrStream *stream);
/* Diagnostics belong to the stream until its next mutation or destruction.
 * Source locations refer to the assembled candidate document. */
size_t trytr_stream_diagnostic_count(const TrytrStream *stream);
const TrytrDiagnostic *trytr_stream_diagnostic_at(const TrytrStream *stream, size_t index);

/* Acquires one reference; snapshots survive later commits and stream destruction.
 * Every acquired/retained reference must be released. NULL release is allowed.
 * The document is borrowed: render/visit it, but do not destroy it. A layout
 * borrowing it must be destroyed before the snapshot's final release. */
TrytrSnapshot *trytr_stream_acquire_snapshot(const TrytrStream *stream);
TrytrSnapshot *trytr_snapshot_retain(TrytrSnapshot *snapshot);
void trytr_snapshot_release(TrytrSnapshot *snapshot);
TrytrDocument *trytr_snapshot_document(const TrytrSnapshot *snapshot);
uint64_t trytr_snapshot_revision(const TrytrSnapshot *snapshot);
size_t trytr_snapshot_source_bytes(const TrytrSnapshot *snapshot);

/* Bounds: 1 MiB assembled source, 64 MiB cumulative candidate parse bytes,
 * eight live snapshot objects (references to one object share its slot).
 * Retention pressure is retryable after releasing an old snapshot. All normal
 * document/arena/VM limits also apply. This layer reparses complete candidates;
 * it does not yet reuse unaffected IR or define a wire transport protocol. */

#endif
