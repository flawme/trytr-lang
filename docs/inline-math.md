# Inline math

Enable inline math explicitly on a heading or paragraph, then use `\(` and
`\)` to mark each formula:

```trytr
@text(inline-math: true) {Let \(x_i^2\) be positive.}
@text(inline-math: true) {The ratio \(\frac{1}{x + 1}\) stays beside the words.}
@heading(inline-math: true) {Working with \(x^2\)}
```

`--render-html` emits native MathML with `display="inline"` inside the paragraph
or heading. The browser aligns it to the text baseline and measures the actual
line height. Fractions, roots, scripts, Greek commands, relations, and unknown
commands use the same notation parser as display math. This is presentation
notation; it does not evaluate or check a proof.

## Compatibility and delimiters

Existing text is literal by default. Without the option, or with
`inline-math:false`, even an unfinished `\(` is ordinary text. Dollar signs
are always literal, so `$5` does not start a formula.

With the option enabled, a doubled backslash outside a formula produces one
literal backslash in native HTML. Write `\\(x\\)` to display the literal text
`\(x\)`. Other text escapes and backslash commands remain literal outside a
formula. Inside formulas, backslashes belong to the notation language except
for the closing fence.

An enabled formula must be nonempty, unnested, and closed on the same source
line. Empty/whitespace-only formulas, unmatched closing fences, nested opening
fences, and CR/LF inside a formula report `TRYTR-E-INLINE-MATH`. Diagnostics
identify the delimiter in the original source, before parameter expansion.
Incomplete notation *inside valid fences* retains the existing visible
command/raw-node fallback; fence validation does not imply mathematical validity.

The option is supported on `@text` and `@heading`. Use `@math` for a display
equation; it does not accept the `inline-math` option. Duplicate options and
non-boolean values are rejected.

## Wrapping and output

Shared wrapping never divides an inline formula at its internal spaces.
Adjacent punctuation and words without separating whitespace stay attached.
A formula longer than the requested width remains whole; the HTML paragraph
can scroll horizontally. Font-aware widths are not implemented yet: logical
widths conservatively count raw-source Unicode scalars, including fences and
commands. Native HTML measures actual line height after MathML typesetting.

SVG, PDF, and plain text keep the entered notation source, including the fences
and doubled backslashes. They use the same formula-aware wrap boundaries.
Native inline typesetting is currently available through the HTML backend and
the per-run MathML API.

## Parameters and ownership

Declared parameter references work in text and inline formulas:

```trytr
@param #n(min:1, max:12, value:3, step:1) {Selected integer}
@text(inline-math:true) {Let \(x = #n\); consider \(x^2\).}
```

Overrides expand before the final runs and notation trees are built. The
paragraph's `text` remains the expanded source, and `inline_runs` is an
immutable document-owned list of text/math spans. Each run records its source
span, content span, and optional math tree. Math source spans include the fences;
content spans exclude them. A doubled backslash has two source bytes and one
content byte.

Run offsets refer to the owning element's expanded `text`. **Math-tree offsets
refer to the run's content slice**, rather than the whole paragraph. A host can
pass the element and run to `trytr_mathml_render_inline_run` from
`<trytr/math/mathml.h>` to get a standalone `<math display="inline">` fragment.
The caller frees the output. Runs and their trees live until the document is
destroyed; layout snapshots borrow them.

The document has a shared budget of 4,096 inline runs, including text spans and
escaped-backslash spans. Each formula uses the existing notation node/depth
bounds and the document's arena limit. No partial inline result is published
after an error. [Streaming append frames](streaming.md) buffer unfinished
paragraphs privately; complete valid frames publish atomically. Partial inline
formulas are never exposed as committed math runs.

Try the example:

```sh
./build/debug/trytr --render-html docs/examples/inline-math.trytr > /tmp/trytr-inline.html
./build/debug/trytr --set n=7 --render-html docs/examples/inline-math.trytr > /tmp/trytr-inline-7.html
```
