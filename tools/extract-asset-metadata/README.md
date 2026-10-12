# extract-asset-metadata

Inspects a single file and reports, to stdout:

- **What it is** — kind and category, detected from file *content* (magic
  bytes, flatbuffer probes, text heuristics, Assimp content sniffing).
  Physical file names carry no meaning: uploaded files are usually renamed
  to storage UUIDs before any tool runs.
- **Key metadata** — image dimensions/channels/format details, mesh
  inventories (`--deep`), WGSL entry points, etc.
- **How `igasset-gen` can consume it** — which plan actions apply
  (`ImageToTexture2DAction`, `GenerateSpritesheetAction`,
  `AssimpToDracoAction`, `CopyWgslSourceAction`) with parameter guidance
  and pipeline caveats.
- **Which other files it depends on** — by logical name, with a status per
  entry (`in-manifest`, `not-in-manifest`, `resolved`, `unmapped`,
  `missing`, `embedded`). This is the flagship feature: an orchestrating
  service uses it to decide how to process each upload and to pre-flight
  its staging manifest before spending an `igasset-gen` job.

Nothing is written to disk; stdout is the only output channel.

## Usage

```
extract-asset-metadata [--json] [--deep] [--manifest <manifest.json>]
                       [--logical-name <name>] [--max-file-size <bytes>]
                       <physical_input_file>
```

| Option | Description |
|---|---|
| `physical_input_file` | Required. The file to inspect (by content, not extension). |
| `--json` | Emit machine-readable JSON instead of text. |
| `--deep` | Opt in to a full Assimp import (models only; images stay header-only). This exposes the **entire Assimp parser attack surface** - only run it in a sandbox. |
| `--manifest <file>` | Staged-asset manifest (JSON, see `schema/staged-asset-manifest.fbs`). Present = staged policy; absent = direct policy. |
| `--logical-name <name>` | Logical name of the input. Default: the physical file's own file name. |
| `--max-file-size <bytes>` | Refuse larger files *before* reading. Default: 1 GiB. |

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Inspected OK (including `kind: unknown` - a successful inspection) |
| 1 | Bad args / unreadable file / invalid manifest |
| 2 | File exceeds `--max-file-size` |
| 3 | `--deep` parse failure (detected as a model but Assimp rejected it) |

The orchestrator should parse the `kind` field of `--json` output for
kind-based policy; `unknown` is deliberately exit 0.

## Modes

- **Direct** (no `--manifest`): trusted local files. Paths resolve with
  historical filesystem semantics; dependency status is checked against
  the filesystem next to the input.
- **Staged** (`--manifest`): untrusted uploads. Every input access resolves
  through the manifest (a deny-by-default capability list - see
  `tools/staged-asset-io`); nothing outside the staged set is ever opened,
  and dependency status is resolved by **pure manifest lookup**, never by
  probing the disk. The physical input is expected to sit in the staging
  root (its manifest `physical_path`).

Pass `--logical-name` in staged mode: manifest lookups and Assimp's
extension-based importer selection both need the logical name (UUID
physical names defeat both). When the input's logical name is not in the
manifest, a runtime entry mapping it to the physical file is synthesized
so `--deep` still works.

Staged manifest logical paths must mirror the *upload tree*, directories
included, while physical files sit flat under the staging root:

```json
{
  "entries": [
    { "logical_path": "foo.gltf", "physical_path": "uuid-a" },
    { "logical_path": "buffers/buf01.bin", "physical_path": "uuid-b" }
  ]
}
```

References inside files (`mtllib`, glTF `uri` values, ...) resolve relative
to the referencing file's logical directory and land on the same manifest
keys.

## Detection

Cheap to expensive, first hit wins: image magic bytes (PNG/JPEG/TGA/BMP/
PSD/GIF/HDR/PIC/PNM, all inspected header-only) -> `igasset`/`igpack`
flatbuffer probes -> text heuristics (OBJ, MTL, glTF JSON, ASCII FBX,
WGSL entry points) -> Assimp content sniff -> `unknown`.

The default mode never decodes pixel data and never parses model files -
only headers and text. `--deep` performs the full Assimp import and
reports the mesh inventory (names/vertices/faces/texcoords/bones),
texcoord+bone selection groups, materials + texture paths, animations, and
journal-observed dependencies (`resolved` / `unmapped` refine the plain
manifest statuses).

Every string echoed into a report comes from an untrusted file and is
sanitized (invalid UTF-8 stripped); dependency names are truncated to 256
code points with a `truncated` flag.

## Tests

`e2e/` (pytest, run against a built binary):

```sh
python3 -m pytest tools/extract-asset-metadata/e2e/ \
    --extract-asset-metadata-bin=build/tools/extract-asset-metadata/extract-asset-metadata
```

or via `EXTRACT_ASSET_METADATA_BIN` (as CI does). The suite covers every
kind, JSON round-tripping, staged mode over nested logical names (UUID
copies + manifest), `--deep` inventories and exit codes, and the full
exit-code contract.