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

## Text and shapes

`@heading`, `@text`, and `@math` take a block of text in braces. The current
renderer shows these as SVG text; math is currently displayed as plain text.

Shapes take integer expressions. The positional values are:

| Element | Values | Example |
| --- | --- | --- |
| Rectangle | x, y, width, height | `@rect(30, 40, 120, 60)` |
| Line | x1, y1, x2, y2 | `@line(30, 40, 150, 100)` |
| Circle | cx, cy, radius | `@circle(90, 80, 25)` |
| Ellipse | cx, cy, rx, ry | `@ellipse(180, 80, 50, 25)` |

You can also use named values, for example:

```trytr
@rect { x: 30 y: 40 width: 120 height: 60 }
@circle { cx: 90 cy: 80 radius: 25 }
```

Paths use the supported uppercase SVG commands `M`, `L`, `H`, `V`, `C`,
`Q`, and `Z` with integer coordinates:

```trytr
@path #curve { M 10 20 C 30 0 50 40 70 20 }
```

Groups translate their children. Nested translations add together:

```trytr
@group #shifted(20, 30) {
    @rect(10, 10, 50, 40)
}
```

The example rectangle appears at document position (30, 40). Coordinates and
translations are limited to `-1,000,000` through `1,000,000`; rectangle sizes,
circle radii, and ellipse radii must be non-negative. Groups can nest up to 64
levels.

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
```

The CLI prints validation errors to standard error and exits nonzero for an
invalid document. The SVG command writes the rendered image to standard output.

## Examples

- [Hello document source](examples/hello.trytr) · [rendered SVG](examples/hello.svg)
- [Shapes and groups source](examples/shapes-and-groups.trytr) · [rendered SVG](examples/shapes-and-groups.svg)
