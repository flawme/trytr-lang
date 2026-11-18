# Native math rendering

Trytr keeps an immutable notation tree for each `@math` element. The CLI and
MCP preview can turn that tree into a standalone MathML document, so a browser
typesets the equation with its own math layout instead of displaying a raster
image or copying TeX source into an SVG text node. The output uses the browser
supported [MathML Core](https://www.w3.org/TR/mathml-core/) vocabulary.

```sh
./build/debug/trytr --render-mathml docs/examples/native-math.trytr
```

For a C caller, include `trytr/math/mathml.h` and call
`trytr_mathml_render(document, &output, &length)`. The returned buffer belongs
to the caller and can be released with `free()`.

The renderer maps fractions to `mfrac`, square roots to `msqrt`, scripts to
`msub`, `msup`, or `msubsup`, numbers and identifiers to `mn` and `mi`, and
explicit arithmetic and relations to `mo`. Common Greek letters and a small
set of named operators are mapped to Unicode math symbols. Parentheses and
brackets remain visible; TeX braces group content without printing braces.
Multiple `@math` elements become separate rows in one MathML block for
`--render-mathml`. `--render-html` instead places one native MathML block at each
equation's source position among paragraphs and canvases. The browser measures
its height and reflows later content; see [document flow](layout.md).

Unknown commands stay visible as literal text, and raw or over-complex
expressions remain readable. This is a deliberately bounded presentation
subset, not a TeX evaluator. The `@math` element is notation; computed values
belong in `@expr`. The existing SVG and text backends still show `@math` source
text. MCP Apps previews omit `@math` text from the vector layer and add a
native MathML panel beside it; the SVG layout still reserves those source
lines. PDF and non-App MCP clients currently use their existing SVG and text
paths.

The C API also exposes `trytr_mathml_render_element` for one validated math
element as a standalone `<math display="block">` fragment. It borrows the
element's text and notation tree while rendering; the caller owns and frees
the returned UTF-8 string. The HTML backend uses this path for display equations.
Paragraphs and headings can enable [inline math](inline-math.md); their runs
render with `trytr_mathml_render_inline_run` as native `display="inline"` MathML.

The MCP preview parses the generated XML and copies only approved MathML Core
elements into the page's MathML namespace. It does not insert document source
or returned markup as HTML.
