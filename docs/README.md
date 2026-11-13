# Writing Trytr documents

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

`@heading`, `@text`, and `@math` take a block of text in braces. The current
renderer shows these as SVG text. In math blocks, `_` and `^` followed by one
character or a balanced brace group render as a subscript or superscript:

```trytr
@math #sequence {x_{i+1} = x^2}
@math #root {√2}
@math #radical {\sqrt{a^2 + b_i}}
@math #ratio {\frac{p}{q}}
```

Use a backslash to show `_`, `^`, or braces literally. This is basic
presentation only: Trytr does not evaluate equations or parse general TeX
commands. The built-in `\sqrt{...}` form draws a radical and overbar; other
math text is shown as entered. The built-in `\frac{...}{...}` form lays out a
smaller raised numerator and lowered denominator around a fraction slash.

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
left and bottom axes, and make a smooth cubic curve through points in the
order written. A constant data axis is centered in the chart. Unlike `@path`, a plot does not require authors
to calculate drawing coordinates. A plot accepts up to 2,048 pairs; each data
coordinate uses the standard `-1,000,000` to `1,000,000` range:

```trytr
@plot #sales(20, 30, 200, 100) {
    0, 12  1, 18  2, 25  3, 22  4, 35
}
```

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

```sh
./build/debug/trytr --render-text docs/examples/text-graph.trytr
```

## Render from an AI chat

The optional MCP server lets an MCP Apps-capable chat client render a Trytr
drawing inside an embedded **Trytr Preview** panel in the conversation. The
panel shows the rendered drawing, zoom controls, and the source used for the
render. It uses Trytr's local vector renderer; it does not call an image
generation service or create a raster image file. The model-facing render call
returns a small preview ID, and the embedded panel loads the SVG from a
separate app-only MCP tool. This keeps SVG markup out of the model's context.
The complete source is rendered after generation, so temporary streamed text
does not determine the final line geometry.

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

See [math-rendering.md](math-rendering.md) for the spline and rasterization
methods used for curves and lines.
