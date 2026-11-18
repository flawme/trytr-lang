# Releasing Trytr

The first release is `v0.1.0-alpha.1`, an experimental Linux C library and CLI
for native chat documents. Its scope, requirements, and gaps are described in
the [release notes](releases/v0.1.0-alpha.1.md). The owner selected Apache 2.0
and authorized public alpha publication after review.

## Candidate checks

1. Keep the public version header, CMake numeric version, MCP metadata, README,
   and release notes consistent. The metadata test checks the alpha suffix.
2. Include LICENSE and NOTICE in the source and installed SDK.
3. Review code and Git history; add regression tests for release findings.
   Record coverage in the [review](reviews/v0.1.0-alpha.1.md).
4. Run the GCC/Clang debug/sanitized and release CI matrix on the exact candidate.
5. In CI, install and relocate the SDK, build a fresh consumer, then extract,
   rebuild, and test the source archive. Retain the GCC release archives and
   `SHA256SUMS` only after those checks pass.
6. Download those retained artifacts, verify their checksums and contents,
   and smoke-test the packaged CLI. Use the CI-built assets for publication.
7. Tag the tested commit, publish the GitHub prerelease and its assets, and
   verify that public metadata, tag, version, and checksums agree.

## Local validation

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

Distribute release builds. The Linux archive targets its build OS and CPU;
other platforms must build from source and are not qualified by this alpha.
PDF export depends on an external browser. The Ubuntu CI runner selects the
installed stable Chrome binary covered by its AppArmor profile, as described in
Chromium's [sandbox notes](https://chromium.googlesource.com/chromium/src/+/main/docs/security/apparmor-userns-restrictions.md).
Browser export has a 30-second deadline and 4 KiB diagnostic cap.

`find_package(Trytr CONFIG REQUIRED)` imports `trytr::core`, `trytr::math`, and
`trytr::proof`. CMake uses its [relocatable package helpers](https://cmake.org/cmake/help/latest/module/CMakePackageConfigHelpers.html).
The numeric SDK package version remains `0.1.0`; runtime and archive names
include `alpha.1`. No stable ABI is promised during the alpha.

Rendering a mathematical claim does not certify it. Conditional algebra
results list unresolved divisor obligations. General proof checking and broad
manual cross-browser typography qualification remain future work.
