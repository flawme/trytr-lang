# Trytr

Trytr is a small C document language for AI chat and scientific explanations.
One `.trytr` source describes text, math, formula plots, vector geometry, and
bounded parameters. Hosts choose how to present it. Native HTML output uses
paragraphs and MathML for mathematics; vector canvases handle drawings.

The project is an experimental 0.1.0 prototype. Language and C APIs can change.
It does not yet provide full TeX typography, pagination, or general theorem
proving. A first public alpha is being prepared; see the
[release plan](docs/releasing.md).

## Build and try

The current development target is Linux with CMake 3.20+, Ninja, and a C23
compiler such as GCC 13 or Clang 18. Python 3 runs the MCP adapter and integration
tests. The core has no TeX, JavaScript, or network dependency.

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
./build/release/trytr --render-html docs/examples/inline-math.trytr > /tmp/trytr.html
./build/release/trytr_stream_demo /tmp/trytr-stream
```

Open the generated HTML in a browser with MathML support. For development with
AddressSanitizer and UndefinedBehaviorSanitizer, use the `debug` presets.

```trytr
@document(version:1) {
    @param #rate(min:1,max:4,value:2,step:1) {Frequency}
    @heading {A short explanation}
    @text(inline-math:true) {Consider \(f(x)=\sin(#rate x)\).}
    @plot(48,20,480,180)(x-min:-3,x-max:3,markers:false) {y=sin(#rate*x)}
}
```

The CLI also supports `--render-text`, `--render-svg`, `--render-mathml`, and
`--set rate=3`. Optional PDF export requires a locally installed Chromium.

## Install and embed

```sh
cmake --install build/release --prefix /tmp/trytr-install
/tmp/trytr-install/bin/trytr --version
cpack --config build/release/CPackConfig.cmake -B build/packages
```

The install includes both CLIs, static libraries, public headers, examples,
and a relocatable CMake package:

```cmake
find_package(Trytr 0.1 CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE trytr::core trytr::proof)
```

Set `CMAKE_PREFIX_PATH` to the installed prefix. `trytr::math` is also available.
The MCP Python adapter currently runs from the source tree.

## Documentation

- [Writing documents](docs/README.md), [inline math](docs/inline-math.md), and
  [native document flow](docs/layout.md).
- [Streaming and atomic host controls](docs/streaming.md): retain valid revisions
  while receiving chunks or changing declared values.
- [Exact algebra checking](proof/README.md): polynomial identities and rational
  identities with explicit divisor conditions, plus narrower proof certificates.
- [Math library](docs/math-library.md) and [language design](design.md).

The checker reports unsupported mathematics as unchecked. A conditional result
is not an unconditional proof. The repository does not yet declare a license;
the owner must select one before a public release.
