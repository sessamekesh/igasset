# Third-Party Notices

This project is licensed under the MIT License.  
It depends on third-party software under other permissive licenses.

This document records the dependencies currently pinned in `extern/CMakeLists.txt`
and the licenses/notice obligations that apply when redistributing source or binaries.

## Included Dependencies

### spdlog

- Source: <https://github.com/gabime/spdlog>
- Pinned version/tag: `v1.16.0`
- License: MIT

`spdlog` depends on `fmt`, which is MIT-licensed (see below).

### flatbuffers

- Source: <https://github.com/google/flatbuffers>
- Pinned version/tag: `v25.12.19`
- License: Apache License 2.0

Apache 2.0 requires preserving license text and required attribution notices.

### igasync

- Source: <https://git.indigocode.dev/indigocode/igasync>
- Pinned revision: `70718c036ae7670e4db3724ec61c6652e9b9b62b`
- License: MIT

`igasync` depends on `concurrentqueue` (see below).

### draco

- Source: <https://github.com/google/draco>
- Pinned version/tag: `1.5.7`
- License: Apache License 2.0

### basis_universal

- Source: <https://github.com/BinomialLLC/basis_universal>
- Pinned version/tag: `v2_0_3`
- License: Apache License 2.0
- Upstream notice file: `NOTICE`

Apache 2.0 requires preserving license text and required attribution notices.
Upstream provides a `NOTICE` file that should be included in redistributions where required.

### glm

- Source: <https://github.com/g-truc/glm>
- Pinned version/tag: `1.0.3`
- License: MIT (dual-licensed with an alternative "Happy Bunny" license)

This project uses the MIT licensing option offered by GLM.

### CLI11

- Source: <https://github.com/CLIUtils/CLI11>
- Pinned version/tag: `v2.6.1`
- License: BSD 3-Clause

### assimp

- Source: <https://github.com/assimp/assimp>
- Pinned version/tag: `v6.0.4`
- License: BSD 3-Clause

### stb (vendored in this repository under `extern/stb`)

- Source: <https://github.com/nothings/stb>
- License: MIT or Public Domain (Unlicense-style alternative)

This project uses the MIT licensing option.

### picosha2

- Source: <https://github.com/okdshin/PicoSHA2>
- Pinned version/tag: `v1.0.1`
- License: MIT

## Known Transitive Dependencies

### fmt (via spdlog)

- Source: <https://github.com/fmtlib/fmt>
- License: MIT

### concurrentqueue (via igasync)

- Source: <https://github.com/cameron314/concurrentqueue>
- Pinned revision in igasync: `65d6970912fc3f6bb62d80edf95ca30e0df85137`
- License: Simplified BSD (2-Clause BSD), with Boost Software License 1.0 alternative

## Redistribution Notes

When distributing source or binaries that include these dependencies:

1. Keep this file (or an equivalent notices file) with the distribution.
2. Include the license texts required by MIT/BSD/Apache dependencies.
3. Preserve Apache 2.0 notice requirements, including upstream `NOTICE` content when applicable.

## Maintenance

Whenever dependency versions are updated in `extern/CMakeLists.txt`, update this file
in the same change to keep notices accurate.
