# First public release

Target `v0.1.0-alpha.1`: an experimental Linux C library and CLI for native
chat documents. The alpha can include inline/display math, formula plots,
bounded integer controls, atomic stream revisions, and exact algebra review.
Full LaTeX compatibility, a general proof assistant, incremental dependency
reuse, and a stable ABI are outside this first release's promise.

## Release gates

- [x] Root quick start with a complete `.trytr` example and honest limitations.
- [x] Adversarial parser, math, streaming, checker, and rendering tests.
- [x] CMake installation, static libraries, public headers, and binary archive
  generation; a fresh consumer checks SDK relocation in release builds.
- [x] A Linux GCC/Clang debug/release CI workflow is defined.
- [ ] Observe that CI workflow pass for the exact proposed release commit.
- [ ] Owner chooses a license and adds the license text and any required notices.
- [ ] Review the tracked tree and history for credentials and private material.
  A credential was exposed in earlier local diagnostics; confirm its rotation
  before publishing related history or logs.
- [ ] Try the source archive and binary package on a separate clean Linux host;
  check native MathML in the browser hosts advertised by the release.
- [ ] Write release notes listing the supported platform, compiler requirements,
  API changes, known rendering gaps, and checker scope.
- [ ] Make the repository public and publish the tagged alpha after those gates.

The repository's visibility and release tag are owner decisions. This document
does not publish either. License selection and actual external validation are
still required even when local tests pass.

## Candidate build

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --install build/release --prefix /tmp/trytr-candidate
cpack --config build/release/CPackConfig.cmake -B build/packages
```

Use release builds for distributed binaries. Sanitized development archives
are not production packages. The first package targets its build OS and CPU;
it is not a portable binary for every Linux distribution. PDF export also
depends on an external Chromium installation; native HTML does not.

`find_package(Trytr CONFIG REQUIRED)` imports `trytr::core`, `trytr::math`, and
`trytr::proof`. Install paths follow CMake's GNUInstallDirs settings. The package
configuration uses CMake's [relocatable package helpers](https://cmake.org/cmake/help/latest/module/CMakePackageConfigHelpers.html).
CI follows GitHub's [workflow syntax](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax).

The alpha deliberately keeps proof review separate from document rendering.
Publishing a document does not certify its mathematical claims. Rational
identity review lists divisor obligations; verifying those assumptions,
transcendental identities, inequalities, and arbitrary prose needs further work.
