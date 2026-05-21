# igasset: C++20 Game asset bundle format + decoder optimized for the web

> [!NOTE] This repository is still under active migration from a closed-source module - it is incomplete, and documentation in this README file is slightly wrong. See Roadmap / TODO section below.

Features:

- Flatbuffer schemas for `igasset` / `igpack` files.
  - Web-optimized game formats + appropriate metadata for unloading in game engines
  - Flatbuffers for low-complexity type-safe serialization/deserialization
- Compressed asset storage types:
  - ~~3D geometry - [Draco](https://github.com/google/draco)~~ (IN PROGRESS)
  - Images / textures
    - RGBA8 Uncompressed
    - Various supercompressed formats via [Basis Universal](https://github.com/binomialLLC/basis_universal)
  - WGSL shader source code - plain text
- `igasset-gen` tool for creating assets from existing formats
  - 3D geometry - anything supported by [Assimp](https://github.com/assimp/assimp) (most common 3D geometry formats)
  - Images - anything supported by [stbi_image](https://github.com/nothings/stb/blob/master/stb_image.h)
- `igpack-bundle` tool for bundling existing `*.igasset` files into `*.igpack` bundles

## Building

There are three main CMake targets built:

- `igasset` - static library, WebAssembly-compatible.
- `igasset-gen` - binary (no WASM)
- `igpack-bundle` - binary (no WASM)

To use the generation tools or the decoder library within a project, run a **native build** using a typical CMake workflow.

`igasset-gen` and `igpack-bundle` cannot be built for WebAssembly - if you plan on using `igasset` in a WebAssembly project, compile the `igasset-gen` and `igpack-bundle` tools separately.

For loading existing `igpack` bundle files, use the `igasset` static library. Example:

```cpp
// TODO (kamaron): example.
```

### CMake Flags

| Flag Name                       | Type + Default         | Description                                                         |
| ------------------------------- | ---------------------- | ------------------------------------------------------------------- |
| `IGASSET_BUILD_GEN_TOOLS`       | **ON** (default) / OFF | Disable to skip building generator binaries and tooling dependencies |
| `IGASSET_BUILD_IGASSET_DECODER` | **ON** (default) / OFF | Disable to skip building the `igasset` library target               |
| `IGASSET_ENABLE_BASISU_SUPPORT` | **ON** (default) / OFF | Disable to remove basisu support. Reduces binary size significantly |

## `igasset-gen` usage:

`igasset-gen` requires one input plan file path as the final positional
argument:

```bash
igasset-gen [options] <input_plan_file>
```

Example:

```bash
igasset-gen                        \
  -i ./test_assets                 \
  -o ./bin/igassets                \
  -s ./schema/igasset-gen-plan.fbs \
  --single-threaded                \
  ./tools/igasset-gen/test-definitions/copy-wgsl-source.igasset-gen.json
```

CLI options:

- `-i, --input_asset_path_root` Root directory used to resolve input asset file paths.
- `-o, --output_path_root` Root directory for generated `*.igasset` files.
- `-s, --schema` Path to `igasset-gen-plan.fbs` (defaults to `igasset-gen-plan.fbs` in the working directory if omitted).
- `-c, --clean` Re-generate all outputs without cache reuse.
- `--single-threaded` Run generation on the main thread (useful for debugging / deterministic troubleshooting).
- `-l, --log_level` Log verbosity: `ERROR`, `WARN`, `DEBUG`, `INFO`, or `TRACE`.

Igasset generation plan files are written in JSON - see [schema/igasset-gen-plan.fbs](schema/igasset-gen-plan.fbs) for the schema. Example plan file:

```json
{
  actions: [
    {
      "action_type": "CopyWgslSourceAction",
      "action": {
        "input_file_path": "some-shader.wgsl",
        "output_file_path": "simple-wgsl.igasset",
        "vertex_entry_point": "vertex-main"
      }
    },
  ]
}
```

See [igasset-gen E2E test-definitions folder](tools/igasset-gen/test-definitions) for some examples.

## `igpack-bundle` usage:

`igpack-bundle` also takes a list of flags, along with a positional argument to execute a single `igpack-bundle.json` plan file.

```bash
igpack-bundle [options] <input_plan_file>
```

Example:

```bash
igpack-bundle                        \
  -i ./bin/igassets                  \
  -o ./build/out                     \
  -s ./schema/igpack-bundle-plan.fbs \
  ./tools/igpack-bundle/test-definitions/three-distinct-assets.igpack-bundle.json
```

CLI options:

- `-i, --input_asset_path_root` Root directory used to resolve input `igasset` file paths.
- `-o, --output_path_root` Root directory for generated `*.igpack` files.
- `-s, --schema` Path to `igpack-bundle-plan.fbs` (defaults to `igpack-bundle-plan.fbs` in the working directory if omitted).
- `-c, --clean` Re-generate all outputs without cache reuse.
- `-l, --log_level` Log verbosity: `ERROR`, `WARN`, `DEBUG`, `INFO`, or `TRACE`.

Igpack bundle generation plan files are written in JSON - see [schema/igpack-bundle-plan.fbs](schema/igpack-bundle-plan.fbs) for the schema. Example plan file:

```json
{
  "igpack_gen_actions": [
    {
      "output_path": "single-wgsl.igpack",
      "igasset_sources": [
        {
          "file_path": "simple-wgsl.igasset",
          "source_type": "WgslSource",
          "igasset_name": "simple-wgsl"
        }
      ]
    }
  ]
}
```

See [igpack-bundle E2E test-definitions folder](tools/igpack-bundle/e2e/test-definitions) for some examples.


## `igasset` library usage:

TODO (kamaorn)

# Roadmap / TODO / Unimplemented (but planned) features:

## New asset types

- [ ] Draco 3D geometry _so dang close to being ready... just need to migrate_
- [ ] Ozz skeletons + animations
- [ ] Cubemap textures + luminosity textures
- [ ] Non-albedo optimized textures (roughness, normal maps)
- [ ] Spritesheets

Eventually I'd also like to get to:

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
