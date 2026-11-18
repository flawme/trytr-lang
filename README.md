# Trytr

**Documents for AI chat: text, mathematics, plots, and controlled interaction
from one compact source.**

[![CI](https://github.com/flawme/trytr-lang/actions/workflows/ci.yml/badge.svg)](https://github.com/flawme/trytr-lang/actions/workflows/ci.yml)
[![License: Apache 2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

Trytr is a declarative document language implemented in C. An AI or author
writes `.trytr`; an application validates it and chooses how to display it.
Native HTML output uses real paragraphs and browser MathML for equations.
Drawings and smooth plots use vector canvases. Text output can travel through
a plain chat or terminal without image data.

**`v0.1.0-alpha.1` is an experimental first release.** The syntax and C ABI can
change. The current tested target is Linux x86_64 with GCC 13 and Clang 18.
This release covers a useful mathematical document subset; full LaTeX
compatibility, pagination, and general theorem proving are future work.

## A complete document

Save this as `wave.trytr`:

```trytr
@document(version:1) {
    @param #rate(min:1,max:4,value:2,step:1) {Frequency}
    @heading {An adjustable wave}
    @text(inline-math:true) {Consider \(f(x)=\sin(#rate x)\).}
    @plot(48,20,480,180)(x-min:-3,x-max:3,markers:false) {y=sin(#rate*x)}
    @math {\frac{1}{1+x^2}}
    @text {The host can change the declared frequency within its bounds.}
}
```

```sh
trytr --render-html wave.trytr > wave.html
trytr --set rate=3 --render-html wave.trytr > faster-wave.html
```

Open either file in a browser with MathML support. No TeX installation,
JavaScript, network connection, or image generation is needed for that output.
The HTML export is static; a host connects parameter metadata to its own
controls. The MCP Apps adapter supplies controls in compatible chat clients.

## Build from source

Requirements: CMake 3.20+, Ninja, GCC 13+ or Clang 18+, and Python 3.10+ for
integration tests and the optional MCP adapter. The C core uses the standard
library and system math library. Builds on other platforms are not validated
by this alpha.

```sh
git clone --branch v0.1.0-alpha.1 https://github.com/flawme/trytr-lang.git
cd trytr-lang
cmake --preset release
cmake --build --preset release
ctest --preset release
./build/release/trytr --render-html docs/examples/inline-math.trytr > /tmp/trytr.html
./build/release/trytr_stream_demo /tmp/trytr-stream
```

Use the `debug` presets for AddressSanitizer and UndefinedBehaviorSanitizer.
The streaming example writes successive offline HTML revisions and shows an
incomplete frame preserving the previous result, followed by a parameter change.

## Output and host support

| Path | What works in this alpha |
| --- | --- |
| `--render-html` | Native paragraphs, inline/display MathML, and vector canvas blocks in reading order. |
| `--render-text` | Text and a bounded Unicode grid for drawings; curves have finite cell resolution. |
| `--render-svg` | Scalable vector geometry; mathematical notation remains source text. |
| `--render-mathml` | Native math fragments for display equations. |
| `--render-pdf` | Optional vector export through a locally installed Chromium-family browser. Math typography follows SVG. |
| C host API | Immutable snapshots, explicit append frames, bounded parameters, and atomic value updates. |
| MCP Apps | Embedded vector/MathML preview and parameter controls when supported by the client. |

The host decides which views and controls it permits. Literal text cannot
produce a continuously smooth curve at arbitrary zoom. There is no arbitrary
document scripting or automatic client integration in the core.

## Check mathematics separately

`trytr-proof` reviews exact polynomial and rational identities, plus two narrow
certificate formats. Put one complete equality per line in a `.trmath` file:

```text
(x+y)^2 = x^2+2xy+y^2
(x^2-1)/(x-1) = x+1
```

```sh
./build/release/proof/trytr-proof claims.trmath
```

The first identity is unconditional. The second is **conditional on `x-1 ≠ 0`**;
cancellation retains the original divisor condition. Conditional claims do not
count as unconditional verification. CLI exit codes are 0 for unconditional
identities, 1 for nonidentities, and 2 for conditional, invalid, or unsupported
input. Symbols are commutative scalar variables: `x(y+z)` means multiplication,
not function application. Functions, inequalities, prose, and assumption
discharge are outside this checker. Rendering an equation never proves it.

## Install and embed

```sh
cmake --install build/release --prefix /tmp/trytr-install
/tmp/trytr-install/bin/trytr --version
```

The install includes `trytr`, `trytr-proof`, static libraries, public headers,
Apache 2.0 license and notices, documentation, and examples. Configure a host
with `-DCMAKE_PREFIX_PATH=/tmp/trytr-install`:

```cmake
find_package(Trytr 0.1 CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE trytr::core trytr::proof)
```

`trytr::math` is also available. CMake's numeric package version is `0.1.0`;
the CLI and release assets include the `alpha.1` suffix. The C API has explicit
ownership: documents own bounded arenas; layouts borrow document data;
retained stream snapshots keep earlier revisions alive. All stream APIs are
single-threaded. Native hosts must respect those lifetimes and serialize access.

The source-tree MCP adapter uses Python's standard library:

```sh
python3 mcp/trytr_mcp.py --trytr ./build/release/trytr --trytr-proof ./build/release/proof/trytr-proof
```

Its transport is stdio; configure it as an MCP server in your chosen client.
The experimental `proof/web` OpenRouter demo is a separate, optional online tool.

## Bounds and current limits

- Document source: 1 MiB; document arena: 4 MiB; nodes: 4,096.
- Streaming: eight live snapshot objects and 64 MiB cumulative candidate parse
  bytes; accepted updates currently reparse the complete document.
- Exact algebra: eight variables, total degree 12, 128 terms per intermediate,
  checked rational coefficients, and explicit work/nesting bounds.
- Text and core math widths use conservative estimates; native MathML supplies
  browser typesetting. Font shaping, pagination, tables, units, typed vector
  expressions, and dependency-based incremental updates remain future work.
- PDF export has a 30-second browser deadline and reports bounded diagnostics.
  Its optional browser must be installed with a working sandbox configuration.

## Further reading

- [Writing documents](docs/README.md), [inline math](docs/inline-math.md), and
  [document flow](docs/layout.md).
- [Streaming and host controls](docs/streaming.md), [math library](docs/math-library.md),
  and [checker scope](proof/README.md).
- [Release notes](docs/releases/v0.1.0-alpha.1.md), [release process](docs/releasing.md),
  [adversarial review](docs/reviews/v0.1.0-alpha.1.md), and [design](design.md).

Licensed under the [Apache License, Version 2.0](LICENSE). See [NOTICE](NOTICE).
