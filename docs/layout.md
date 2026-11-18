# Document flow

The shared module under `src/layout/` owns line breaking, conservative text
metrics, geometry bounds, and immutable flow snapshots. SVG uses its wrapping
and bounds code; HTML and text output consume snapshots. Layout does not change
the document's notation trees or IR.

## Reading order

A heading, paragraph, or display equation forms one block. Consecutive drawing
elements form one canvas block. A text or math element ends that canvas run,
so explanations can appear before, between, and after diagrams. Shape
coordinates stay local. Nested group translations compose before bounds are
calculated; each snapshot element retains its translation, source index, and ID.

Without fixed document dimensions, each canvas fits its geometry with 16 units
of padding; plot bounds also allow room for tick labels. With fixed dimensions,
each run uses the document's `(0,0,width,height)` viewport. Existing SVG output
keeps its coordinate canvas and text placement; flow is another host view.

Text wraps at spaces and tabs without splitting words or UTF-8 scalars.
Unbreakable words can exceed the requested width. LF, CR, and CRLF create line
breaks; empty lines are retained. Paragraph ends are cached so narrow wrapping
does not repeatedly scan a long paragraph. Widths use conservative Unicode
scalar counts, not shaped glyph advances.

## Native HTML output

```sh
./build/debug/trytr --render-html docs/examples/flow-math.trytr > /tmp/trytr-flow.html
./build/debug/trytr --set rate=4 --render-html docs/examples/flow-math.trytr > /tmp/trytr-flow-4.html
```

Open either file in a browser. Headings and paragraphs are native HTML, each
equation is a native MathML block at its source position, and canvas blocks
use SVG. The HTML contains no scripts, network resources, or raster images.
Source text is escaped. Overrides resolve before layout, using the existing
parameter parser.

HTML uses a 640-unit text width, reduced by narrower author-provided widths.
Equations are not word-wrapped by this pass. The browser measures each native
equation and positions later blocks after its actual height. Canvas viewports
and long content scroll horizontally when they exceed the host width.

Paragraphs and headings may opt into [inline math](inline-math.md). Inline
formulas remain atomic during wrapping; native MathML determines their baseline
and actual line height. Source-based box estimates still require native host
measurement before placing later blocks.

`--render-text` also interleaves canvas drawings with surrounding paragraphs.
It preserves plain text and math source, applies explicit paragraph widths
with the shared wrapping rules, and uses its bounded character grid for shapes.

## C API and ownership

```c
#include <trytr/layout.h>

TrytrLayoutOptions options = {.width = 640, .gap = 8};
TrytrLayout *layout = NULL;
TrytrStatus status = trytr_layout_create(document, &options, &layout);
if (status == TRYTR_STATUS_OK) {
    for (size_t i = 0; i < trytr_layout_block_count(layout); ++i) {
        const TrytrLayoutBlock *block = trytr_layout_block_at(layout, i);
        /* Inspect kind, dimensions, position, element and line ranges. */
        (void)block;
    }
}
trytr_layout_destroy(layout);
```

`NULL` options use author-only wrapping and an 8-unit block gap. Explicit zero
width also means author-only wrapping; zero gap adds no extra space. The
document must outlive its layouts: text, IDs, and math trees are borrowed.
Snapshot arrays have separate ownership and are released with
`trytr_layout_destroy`. Multiple snapshots can coexist without changing the
document. Out-of-range access returns `NULL`; failure publishes no partial
snapshot.

Limits are 4,096 elements/blocks, 65,536 visual lines, and extents within one
billion units. Options use the standard coordinate limit. HTML output is
bounded to 8 MiB; text retains its 2 MiB limit.

`<trytr/html.h>` exposes `trytr_html_render`. Math hosts can also call
`trytr_mathml_render_element` from `<trytr/math/mathml.h>` for a single validated
math element. Both allocate UTF-8 buffers that the caller frees.

## Remaining work

This pass supports display and opt-in inline equations. Font
shaping, font-aware core math measurement, explicit nested flow/canvas
containers, pagination, and dependency-based incremental layout remain future work.
Snapshot math dimensions estimate source text; native hosts must measure math
and reflow subsequent blocks, as the HTML backend does.

[Streaming append frames](streaming.md) can publish new document revisions and
retain older ones while rendering. Layouts still borrow their document; hold a
snapshot reference until its layouts are destroyed.
