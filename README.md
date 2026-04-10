# igasset: C++20 Game asset bundle format + decoder optimized for the web

Included in this repository are:

- `igasset` Flatbuffer schema
  - Web-optimized game formats + appropriate metadata for unloading in game engines
  - Flatbuffers for low-complexity type-safe serialization/deserialization
- Compressed asset storage types:
  - 3D geometry - [Draco](https://github.com/google/draco)
  - Images / textures
    - RGBA8 Uncompressed
    - Various supercompressed formats via [Basis Universal](https://github.com/binomialLLC/basis_universal)
  - WGSL shader source code - plain text
- `igasset-gen` tool for creating assets from existing formats
  - 3D geometry - anything supported by [Assimp](https://github.com/assimp/assimp) (most common 3D geometry formats)
  - Images - anything supported by [stbi_image](https://github.com/nothings/stb/blob/master/stb_image.h)
- `igpack-gen` tool for bundling existing `*.igasset` files into `*.igpack` bundles

## Building

There are three main CMake targets built:

- `igasset` - static library, WebAssembly-compatible.
- `igasset-gen` - binary, not WebAssembly-compatible.
- `igpack-gen` - binary, not WebAssembly-compatible.

To use the generation tools or the decoder library within a project, run a **native build** using a typical CMake workflow.

`igasset-gen` and `igpack-gen` cannot be built for WebAssembly - if you plan on using `igasset` in a WebAssembly project, compile the `igasset-gen` and `igpack-gen` tools separately.

### CMake Flags

| Flag Name                       | Type + Default         | Description                                                         |
| ------------------------------- | ---------------------- | ------------------------------------------------------------------- |
| `IGASSET_BUILD_GEN_TOOLS`       | **ON** (default) / OFF | Disable to skip building generator binaries and tooling dependencies |
| `IGASSET_BUILD_IGASSET_DECODER` | **ON** (default) / OFF | Disable to skip building the `igasset` library target               |
| `IGASSET_ENABLE_BASISU_SUPPORT` | **ON** (default) / OFF | Disable to remove basisu support. Reduces binary size significantly |

## `igasset-gen` usage:

TODO (kamaron)

## `igpack-gen` usage:

TODO (kamaron)

## `igasset` library usage:

TODO (kamaorn)

# Roadmap / TODO / Unimplemented (but planned) features:

## New asset types

- [ ] Ozz skeletons + animations
- [ ] Cubemap textures + luminosity textures
- [ ] Non-albedo optimized textures (roughness, normal maps)
- [ ] Spritesheets
- [ ] Sounds
- [ ] Recast / detour navmeshes

## Helpful utilities / features

- [ ] Asset metadata sidecar files (compression ratio, etc.)
- [ ] Asset viewers
- [ ] Memory limits
- [ ] Runtime profiling

# Contributing Guidelines

This library is primarily intended for use within Indigo Code projects, and is published as-is primarily as a resource and potential tool for indie game engine authors to use if they'd like a starting-off asset processing tool.

It is _not_ intended to be general-purpose or robust to a wide variety of formats! Feel free to request additional formats, but please understand that most requests will be denied. I intend to keep the list of supported formats for each asset modality minimal, and scoped specifically to formats that excel at web delivery.

Please also be aware that this is **a very opinionated format and library**. I don't see myself prioritizing any efforts around extensibility / robustness - I use it for my own projects and welcome you to use the code (in part or in full!) as you please, but am not interested in making breaking changes to satisfy a wider audience.

Criteria of a web-friendly format:

- Small size over-the-wire
- Lossless or acceptably lossy compression (preferrably)
- Reasonable _decoding_ time on the client
- Added code has a reasonable binary size when linked in a WebAssembly application
