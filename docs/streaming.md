# Streaming append frames

`<trytr/stream.h>` lets a host receive arbitrary byte chunks while rendering the
last committed document. The host chooses frame boundaries: a frame contains
document-body elements, without an `@document` wrapper. There is no change to
`.trytr` syntax. Frames can contain several elements, complete groups, parameter
declarations before content, and comments.

## Publication and recovery

Create a stream to obtain revision 0, an empty document. Begin a frame against
the expected revision, feed its chunks, then commit when it is ready:

```c
#include <trytr/stream.h>

TrytrStream *stream = NULL;
TrytrStatus status = trytr_stream_create(NULL, &stream);
bool changed = false;
if (status == TRYTR_STATUS_OK)
    status = trytr_stream_begin_append(stream, 0);
if (status == TRYTR_STATUS_OK)
    status = trytr_stream_feed(stream, "@text{Hello", 11);
if (status == TRYTR_STATUS_OK)
    status = trytr_stream_feed(stream, ", world.}", 9);
if (status == TRYTR_STATUS_OK)
    status = trytr_stream_commit_append(stream, &changed);

/* If status is OK and changed is true, revision 1 is available. */
TrytrSnapshot *snapshot = trytr_stream_acquire_snapshot(stream);
/* Render trytr_snapshot_document(snapshot) using HTML, text, SVG, or a host. */
trytr_snapshot_release(snapshot);
trytr_stream_destroy(stream);
```

`feed` only buffers bytes. Chunks may split UTF-8 characters, math delimiters,
comments, or any other token. It accepts all supplied bytes or none. It does
not parse incomplete input or change the current revision.

`commit_append` assembles and parses the entire candidate, validates it,
compiles its IR, and checks execution limits before publishing a new snapshot.
Publication is atomic. Errors leave both the current snapshot and pending
frame intact. Feed more bytes to complete an unfinished frame, retry after
releasing retained snapshots, or call `cancel_append` to discard the frame.
Inspect stream diagnostics before the next mutation clears them. Diagnostic
locations address the assembled document, including its generated wrapper.

Renderer limits still apply separately: a valid committed document can exceed
a particular backend's layout or output budget. A host should keep its previous
display until rendering the new snapshot succeeds.

Empty frames succeed without advancing the revision. Nonempty accepted frames
advance it even when they contain only whitespace or comments. A stale
`expected_revision` returns `TRYTR_STATUS_REVISION_CONFLICT`; it does not open a
frame. `finish` requires no pending frame and is idempotent. Finished streams
reject further appends while leaving their last snapshot readable.

## Atomic host controls

`trytr_stream_set_parameters` changes selected declared values against an
expected revision. Unspecified values persist, including across future append
frames. Changing a value rebuilds all bound text, inline math, geometry, and
formula plots before publishing one new revision:

```c
TrytrParameterOverride updates[] = {{.id = "rate", .value = 3}};
bool changed = false;
TrytrStatus status = trytr_stream_set_parameters(stream,
    trytr_stream_revision(stream), updates, 1, &changed);
```

Updates are allowed in `IDLE` and `FINISHED`, so host controls can continue
working after an AI response ends. They are rejected while an append frame is
open. Unknown or duplicate IDs, invalid bounds/steps, and any resulting parse
or execution failure leave the published values unchanged. A batch is atomic.
An empty update or unchanged values succeed with `changed=false` and consume
neither a revision nor parse budget. Stale revisions still conflict on no-ops.

Old snapshot handles keep their previous values. A parameter-only revision has
the same source byte count: overrides are data and are never spliced into the
source text. Rebuilds share the same retention and cumulative parse budgets as
append commits. Value changes do not reopen a finished stream.

## Ownership and bounds

Every acquired or retained snapshot reference needs a matching
`trytr_snapshot_release`. Snapshots survive later commits and stream destruction.
Their document is borrowed: callers must not destroy it. Layouts borrow that
document's strings and math trees, so destroy a layout before releasing its last
snapshot reference. No syntax node is individually reference counted.

All stream and snapshot APIs are single-threaded. Hosts must serialize access,
including rendering and reference operations; the reference counts are not
atomic. `NULL` stream options select fitted canvases. For fixed canvases, set
both `canvas_width` and `canvas_height` to values from 1 through 1,000,000.

The initial implementation bounds:

- Assembled source to 1 MiB, including the generated wrapper and frame separators.
- Cumulative candidate parse bytes to 64 MiB, including rejected attempts.
- Live committed snapshot objects to eight per stream; several references to
  one snapshot share a slot. An unretained current snapshot can be replaced at
  the limit. Publication briefly also owns its candidate document.
- Each document's arena, nodes, parameters, math runs, and VM work to the normal
  language limits.

`TRYTR-E-STREAM-SNAPSHOTS` is retryable after an old snapshot is released.
`TRYTR-E-STREAM-WORK` exhausts that stream's cumulative parse budget. Source
overflow rejects the entire feed without consuming bytes. Stream destruction
also releases pending input and rejected candidate diagnostics.

This API currently appends source and reparses complete candidates. It does not
reuse unaffected IR, edit published elements, choose
token/frame boundaries automatically, or define a network transport. Hosts may
use their own framing and render each accepted revision with existing backends.

## Runnable example

```sh
cmake --build build/debug
./build/debug/trytr_stream_demo /tmp/trytr-stream
```

The small [C example](../examples/stream_demo.c) produces five offline HTML files:

1. `trytr-stream-1.html`: a heading and declared frequency parameter.
2. `trytr-stream-2.html`: the heading plus native inline math.
3. `trytr-stream-pending.html`: the same revision 2 while a plot is incomplete.
4. `trytr-stream-3.html`: the completed formula plot after publication.
5. `trytr-stream-4.html`: frequency changed atomically from 2 to 3 after finish.

The output uses the core streaming API and native HTML/MathML backend. Only the
plot canvas uses vector markup. There are no scripts or network dependencies.
