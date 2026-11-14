# Writing Trytr documents

Trytr is intended to be a compact, declarative document language between an AI
and the app that presents its answer. The document describes meaningful things
such as text, equations, data, diagrams, and permitted user inputs; a host then
chooses how to show them. SVG is one current output path, not the language's
purpose or required output format. A host may also choose direct text, native
widgets, print, or other renderers as those backends are built.

This is broader than replacing TeX syntax. The goal is to avoid requiring a
large TeX installation for common mathematical and explanatory content, while
also representing non-math content in the same small runtime. Math is one
semantic content type alongside plots, structured data, tables, diagrams, and
interactive explanations.

An important target is bounded interactivity chosen by the developer. For
example, an AI-authored explanation could expose a developer-approved numeric
value; changing it would update the linked formula, plotted data, and labels
together. The document would describe the allowed control and its range, while
the host remains responsible for which controls it permits. The prototype
supports bounded integer parameters, live controls in MCP Apps, formula plots,
and scalar math expressions. It does not yet provide incremental dependency
updates or typed complex/vector values in the document language. This keeps
the examples below honest about what works today and what the language is
being built toward.

Trytr files are plain text documents. A file starts with a versioned document
block; elements inside it are written in source order:

```trytr
@document(version: 1) {
    @heading #title {Hello, Trytr}
    @text {A short introduction goes here.}
    @rect #frame(30, 110, 320, 90)
}
```

An optional `#id` gives an element a stable name. IDs must be unique in the
whole document.

Documents can set a fixed canvas by providing both `width` and `height` in the
document options. Each dimension ranges from 1 through 1,000,000 document
units. The canvas starts at `(0, 0)`; content beyond its edges is clipped. If
the dimensions are omitted, SVG output continues to fit itself around the
document's content with padding:

```trytr
@document(version: 1, width: 640, height: 480) {
    @rect(20, 20, 600, 440)
}
```

## Text and shapes

`@heading`, `@text`, and `@math` take a block of text in braces. The SVG and
text renderers show the entered math source. The MCP Apps preview also typesets
`@math` elements natively with MathML. In math blocks, `_` and `^` followed by
one character or a balanced brace group render as a subscript or superscript in
that MathML output:

```trytr
@math #sequence {x_{i+1} = x^2}
@math #root {√2}
@math #radical {\sqrt{a^2 + b_i}}
@math #ratio {\frac{p}{q}}
```

Use a backslash to show `_`, `^`, or braces literally. `@math` does not
evaluate equations or parse general TeX commands. The native MathML renderer
handles `\sqrt{...}` and `\frac{...}{...}`, simple arithmetic and relations,
Greek letter commands, and scripts. Other commands stay visible as literal
text. See [native math rendering](mathml-rendering.md) for current limits.

The document C API also exposes a bounded notation tree for `@math` and the
formatted result of `@expr`. Visitors can inspect rows, identifiers, numbers,
groups, fractions, roots, scripts, explicit arithmetic, and relations through
`TrytrDocumentElement.math_tree`; source offsets point into that element's
original text. Unknown TeX commands remain command nodes, and inputs beyond
the tree-size bound remain available as one raw node. The MathML renderer uses
this tree; the SVG and text renderers continue to preserve the original source.
See
[the math notation API notes](math-notation.md).

For a computed scalar, use `@expr` with a bounded expression. Its result appears
as math text in the rendered document:

```trytr
@expr #root-value {sqrt(2)}
@expr #distance {hypot(3, 4)}
@expr #angle {atan2(1, 1) * 180 / pi}
```

Expressions support decimal and scientific numbers, `pi`, `e`, `+`, `-`, `*`,
`/`, `^`, parentheses, unary signs, and the pure functions `sqrt`, `abs`,
`sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `exp`, `log`, `log10`, `sinh`,
`cosh`, `tanh`, `pow`, `atan2`, `hypot`, `min`, and `max`. Trigonometric
functions use radians. Domain errors, non-finite results, unknown functions,
and expressions beyond the implementation limits are rejected during parsing.

The implementation also has a small, separately linked C math library under
`include/trytr/math` and `src/math`. It currently separates real scalars,
complex numbers, points, 2D vectors, matrices, affine transforms, and curve
geometry. A vector can be converted explicitly to `x + yi`; its rotation uses
complex multiplication, while dot and cross products stay vector operations.
The scalar math layer is available to both `@expr` and formula plots. The
complex, vector, matrix, transform, and curve APIs remain lower-level C library
modules for now; their representations and numerical boundaries are in [the
math library notes](math-library.md).

Text blocks can set a font size, RGB color, and alignment in a parenthesized
option list. Font sizes range from 1 through 256 document units; the defaults
remain 28 for headings, 18 for math, and 16 for text. `@heading` and `@text`
can also set a width from 1 through 1,000,000 document units. Trytr wraps these
blocks at word boundaries within that width; a word wider than the box stays
intact. Center and right alignment use the box width when provided. Otherwise,
they align explicit lines to the widest line using the renderer's current
conservative width estimate:

```trytr
@text #caption(font-size:18, color:rgb(40,70,110), align:center, width:320) {
    A centered line
    A shorter line
}
```

Shapes take integer expressions. The positional values are:

| Element | Values | Example |
| --- | --- | --- |
| Rectangle | x, y, width, height | `@rect(30, 40, 120, 60)` |
| Rounded rectangle | x, y, width, height, corner radius | `@roundrect(30, 40, 120, 60, 10)` |
| Line | x1, y1, x2, y2 | `@line(30, 40, 150, 100)` |
| Circle | cx, cy, radius | `@circle(90, 80, 25)` |
| Ellipse | cx, cy, rx, ry | `@ellipse(180, 80, 50, 25)` |
| Polygon | At least three integer x/y pairs in a block | `@polygon { 10,10 30,10 20,30 }` |
| Plot | x, y, width, height plus at least two numeric data pairs | `@plot(20, 30, 200, 100) { 0,10 1,25 2,18 }` |

Plots map the data's numeric x and y ranges into the chart rectangle, draw
left and bottom axes with readable numeric ticks, light grid lines, and a smooth
cubic curve through points in the order written. Coordinates may be decimal or
scientific notation. Sample markers make the input data visible. A constant
data axis is centered in the chart. Unlike `@path`, a plot does not require
authors to calculate drawing coordinates. A plot accepts up to 2,048 pairs;
each data coordinate uses the standard `-1,000,000` to `1,000,000` range:

```trytr
@plot #sales(20, 30, 200, 100) {
    0, 12  1, 18  2, 25  3, 22  4, 35
}
```

Plot appearance can be adjusted after the chart bounds. Grid lines and sample
markers are on by default; a series color and line width can be set directly:

```trytr
@plot #sales(20, 30, 200, 100) (stroke:rgb(37,99,235), stroke-width:3,
                                 grid:true, markers:true) {
    0, 12  1, 18  2, 25  3, 22  4, 35
}
```

Use `grid:false` or `markers:false` to turn either feature off. Plot colors use
the same `rgb(red, green, blue)` form as other shapes; width is from 0 through
32 document units.

For a function, start the plot block with `y =` and set its integer x range.
Trytr samples the expression at evenly spaced (fractional) x values, then
builds a smooth cubic path through the evaluated points. The default range is -10 through 10 and the
default is 257 samples; `samples` may be set from 2 through 1,024:

```trytr
@plot #wave(20,30,400,180)(x-min:-8,x-max:8,samples:321,markers:false) {
    y = sin(x) / (1 + x^2)
}
```

Formula plots use the same scalar functions as `@expr`, plus the independent
variable `x`. A sample where the function is undefined, non-finite, or outside
the coordinate limit invalidates the plot. Sample count and expression size
are bounded, and decimal precision is preserved through SVG and text rendering.

You can also use named values, for example:

```trytr
@rect { x: 30 y: 40 width: 120 height: 60 }
@roundrect { x: 30 y: 40 width: 120 height: 60 radius: 10 }
@circle { cx: 90 cy: 80 radius: 25 }
```

Rectangles, rounded rectangles, circles, ellipses, paths, and polygons accept
`fill`, `stroke`, and `stroke-width`; lines accept `stroke` and `stroke-width`.
Colors use `rgb(red, green, blue)` with each channel from 0 through 255. Stroke
width is an integer from 0 through 32 document units; the default is 1. For
example:

```trytr
@rect #card {
    x: 30 y: 40 width: 120 height: 60
    fill: rgb(230, 57, 70)
    stroke: rgb(20, 40, 60)
    stroke-width: 3
}
```

Without a `fill`, closed shapes stay unfilled. Without a `stroke`, shapes use
the default dark outline.

Paths use the supported uppercase SVG commands `M`, `L`, `H`, `V`, `C`,
`Q`, and `Z` with integer coordinates:

```trytr
@path #curve { M 10 20 C 30 0 50 40 70 20 }
@path #colored(stroke: rgb(100, 40, 160), stroke-width: 3) {
    M 10 20 C 30 0 50 40 70 20
}
```

Path and polygon style options go in parentheses after the optional ID and
before the geometry block. Filling an open path uses the closed area described
by its path, as in SVG.

Polygons use a whitespace or comma separated list of at least three coordinate
pairs. Their coordinates use the same range as other shapes.

Groups translate their children. Nested translations add together:

```trytr
@group #shifted(20, 30) {
    @rect(10, 10, 50, 40)
}
```

The example rectangle appears at document position (30, 40). Coordinates and
translations are limited to `-1,000,000` through `1,000,000`; rectangle sizes
and all shape radii must be non-negative. Groups can nest up to 64 levels. A
rounded rectangle's radius must also be at most half its width and half its
height.

Integer expressions support unary `+` and `-`, parentheses, `+`, `-`, `*`,
and `/`. Multiplication and division have higher precedence; division truncates
toward zero. For example, `@rect(2 + 3 * 4, 10, 100 / 2, 30)`.

## Parameters and shared values

Declare bounded integer parameters at the start of the document. Each requires
a minimum, maximum, default value, step, and a reader-facing label:

```trytr
@param #peak(min:8, max:48, value:24, step:2) {Peak height}
```

Use `#peak` inside integer expressions, text or math blocks, `@expr`, formula
plots, or as a numeric plot data coordinate. The selected value is resolved
consistently across those locations. Parameter references can use the
arithmetic operators above in integer expressions. Parameters must be declared
before document content and cannot be nested in groups. The current implementation supports at most 128
parameters; values are bounded to the document coordinate range.

The C API accepts a validated parameter override while parsing a document. The
CLI offers the same operation with `--set ID=INTEGER`; rendering then uses the
new value for the equation and graph together:

```sh
./build/debug/trytr --render-svg docs/examples/interactive-peak.trytr
./build/debug/trytr --set peak=38 --render-svg docs/examples/interactive-peak.trytr
```

MCP Apps hosts receive each parameter's label, range, default, current value,
and step with the preview. The embedded panel displays an accessible slider
for each declaration and re-renders the document as values change. Hosts can
also pass a `parameters` object with `render_trytr` or `render_trytr_text`; the
same bounds and step validation used by the C API and CLI is applied before
rendering.

## Build, validate, and render

From the project root:

```sh
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
./build/debug/trytr docs/examples/hello.trytr
./build/debug/trytr --dump-ir docs/examples/shapes-and-groups.trytr
./build/debug/trytr --render-svg docs/examples/shapes-and-groups.trytr > /tmp/shapes-and-groups.svg
./build/debug/trytr --render-svg docs/examples/proof-sqrt2.trytr > /tmp/proof-sqrt2.svg
./build/debug/trytr --render-mathml docs/examples/native-math.trytr
```

The CLI prints validation errors to standard error and exits nonzero for an
invalid document. The SVG command writes the rendered image to standard output.
Use `--render-text` to write text blocks followed by a compact text drawing
of the vector geometry. It uses connected Unicode strokes for plot and path
curves, with antialiased line rasterization and text outlines for other shapes.
This output contains no SVG or image data; it is meant to be copied directly
into a chat message or terminal. The text backend keeps shape outlines and
curves recognizable, while omitting color and fill styling. A text grid has
finite cell resolution, so it cannot display a mathematically smooth curve at
arbitrary zoom. Use SVG for continuous, scalable geometry.

Use `--render-mathml` to write a MathML Core document for each `@math`
element. Browsers typeset formulas using native math layout; this backend does
not require a TeX installation.

```sh
./build/debug/trytr --render-text docs/examples/text-graph.trytr
```

### Export a PDF

Keep the `.trytr` file as the editable source, then compile it to a vector PDF:

```sh
./build/debug/trytr --render-pdf docs/examples/proof-sqrt2.trytr > proof-sqrt2.pdf
```

PDF export uses Chromium or Google Chrome installed on the build machine; CMake
detects it when configuring. The renderer preserves the SVG curves and shapes
as vector content. A fixed `width` and `height` in `@document` set the page
canvas; without them, the single PDF page fits the document's rendered content.
This is an initial source-to-PDF workflow: Trytr does not yet add automatic page
breaks, multi-page layout, citations, or a browser-based editor.

## Render from an AI chat

The optional MCP server is one host adapter. In an MCP Apps-capable chat client,
it renders a completed Trytr document inside an embedded **Trytr Preview**
panel. The current panel has zoom controls, a source view, and a native MathML
area for `@math` elements. Its vector layer keeps layout spacing but leaves
equation typesetting to the MathML area. It uses Trytr's local renderers; it
does not call an image-generation service or create a raster image file. The
model-facing render call returns a small preview ID, and
the panel loads the SVG using a separate app-only MCP tool. This keeps SVG
markup out of the model's context. The complete source is rendered after
generation, so temporary streamed text does not determine the final line
geometry. Future host adapters can provide different outputs and approved
interactions without making MCP Apps or SVG part of the language definition.

The view follows the open [MCP Apps UI standard](https://github.com/modelcontextprotocol/ext-apps/blob/main/specification/2026-01-26/apps.mdx).
Chat clients that do not support MCP Apps receive the character-grid text
rendering instead of the embedded vector view. That fallback has finite cell
resolution; the host must support the embedded preview to display a smooth
curve at arbitrary zoom.

Build Trytr first, then add a server entry to the chat client's MCP
configuration, using an absolute path to this checkout:

```toml
[mcp_servers.trytr]
command = "python3"
args = ["/absolute/path/to/trytr-lang/mcp/trytr_mcp.py"]
```

The server uses `build/debug/trytr` by default and has no Python package
dependencies. It accepts up to 1 MiB of source and returns previews up to 2 MiB.
It keeps at most four previews in memory for ten minutes; restarting the server
clears them, so render the source again if an old preview is unavailable.
Set `TRYTR_EXECUTABLE` or pass `--trytr /path/to/trytr` when using a different
build. Restart the chat client after registering the server. Then
ask it to create a drawing in Trytr and call `render_trytr`; invalid documents
return parser and validation diagnostics for the AI to correct. The first
render appears as a separate embedded view when the chat client supports MCP
Apps; otherwise either render tool returns a text-grid fallback.

For a text-only response, call `render_trytr_text` with the same Trytr source.
Clients without MCP Apps support receive text blocks and a finite-resolution
character-grid drawing directly in chat. MCP Apps clients open the scalable
vector preview, even if the model selected the text fallback. This keeps the
final graph smooth where the host can render it, while retaining usable text
output for plain chat and terminal clients.

## Examples

- [Hello document source](examples/hello.trytr) · [rendered SVG](examples/hello.svg)
- [Shapes and groups source](examples/shapes-and-groups.trytr) · [rendered SVG](examples/shapes-and-groups.svg)
- [Proof that sqrt(2) is irrational](examples/proof-sqrt2.trytr) · [rendered SVG](examples/proof-sqrt2.svg)
- [Text graph source](examples/text-graph.trytr) · [text rendering](examples/text-graph.txt)
- [Numeric plot source](examples/data-plot.trytr) · [SVG](examples/data-plot.svg) · [chat text](examples/data-plot.txt)
- [Shared parameter source](examples/interactive-peak.trytr) · [default render](examples/interactive-peak.svg) · [render with `peak=38`](examples/interactive-peak-38.svg)
- [Formula plot with host controls](examples/formula-controls.trytr) · [default render](examples/formula-controls.svg)
- [Native math source](examples/native-math.trytr) · render with `--render-mathml` or an MCP Apps preview

See [math-rendering.md](math-rendering.md) for the spline and rasterization
methods used for curves and lines.
