# Staged Asset Pipeline — Master Plan & Status (Handoff Document)

> **Audience:** an AI coding agent (or human) picking this work up in a fresh
> session/chat. This document is intentionally self-contained: it records the
> full design (including every decision agreed with the user across a long
> planning phase), the exact completion status of each phase, and all
> non-obvious technical facts learned so far, so that **no prior conversation
> context is required** to continue safely.
>
> **Last updated:** 2026-10-12 (session 2) — **PHASE 2 COMPLETE** (milestone
> 4): detectors + `--deep` (milestone 3), fixtures + 31-test pytest e2e +
> CI workflow update (milestone 4), all validated on GCC and clang; a
> latent Phase-1 bug fixed in AssimpStagedIOSystem (nested logical paths,
> §4.6). Phase 3 (§6) is next. Earlier decisions: FBX fixture from
> assimp's vendored box.fbx (§5.10), clang parity ran early (§8/§10),
> milestone commits per phase (§0.5).
> **Repo:** `/home/kamaron/dev/igasset`.

## 0. TL;DR status

| Phase | Scope | Status |
|---|---|---|
| 0 | Swap the async I/O facade for sync; drop io thread pool, keep exec pool | **COMPLETE, validated** (§3) |
| 1 | `tools/staged-asset-io` lib + manifest schema + Assimp v6.0.5 + importer narrowing to FBX/glTF/OBJ | **COMPLETE, validated** (§4) |
| 2 | `tools/extract-asset-metadata` CLI (the flagship new tool) | **COMPLETE, validated** (GCC + clang): CLI, renderers, all detectors, deps + `--deep`, fixtures, pytest e2e (31 tests), CI workflow updated (§0.5) |
| 3 | Rewire `igasset-gen` onto the staged facade + fix OOB/overflow bugs | **NOT STARTED** — full spec in §6 |

### 0.5 Resume audit + session-2 decisions (2026-10-12)

The previous session crashed mid-Phase-2 (single 04:22 batch of partials,
no .cc files): modified `tools/CMakeLists.txt` +
`staged-asset-manifest.h` (declared `with_extra_entry`) + selftest calls
(10 new checks), and 5 untracked files under `tools/extract-asset-metadata/`
(CMakeLists + 4 headers). Session 2 kept all of it (user decision: keep +
complete) and landed milestone 1:

- `with_extra_entry` implemented (copy-construct + validate; identical
  duplicate tolerated, conflicting duplicate rejected; selftest now
  **77 checks ALL PASS**).
- 7 source files created; `main.cc` is final per the locked CLI contract:
  exit codes 0/1/2/3 verified by hand; SHA-256 chunked via
  `picosha2::hash256_one_by_one` (cross-checked against sha256sum);
  TOCTOU guard re-checks size during read; CLI11 errors forced to exit 1;
  spdlog goes to **stderr** (stdout is the report channel), level `err`.
- Renderers (`report-text.cc` incl. all `to_string`s, `report-json.cc`)
  are complete — they are pure functions of the finished metadata structs.
- Detector bodies (`detect-image/model/shader`, `deep-import`) are honest
  stubs returning nullopt / failure; every file currently reports
  `kind: unknown` (correctly, exit 0).
- **`sniff_assimp` signature changed** (header edit): returns
  `optional<ModelScan>` (kind + importer_name) instead of `optional<string>`
  so Kind is derived from the importer index, never string matching.
- Deep-failure path renders the report then exits 3 (decision: partial
  report + distinct code beats silent failure).
- Duplicate dependency references are deduped by normalized name at
  finalize time (sources joined ", ") — light-scan and deep-journal
  references to the same file merge into one entry.
- Validation: selftest 77/77; both e2e suites 23/23 (direct-mode
  invariant); smoke tests for all four exit codes; clang-format clean.

User decisions for the remaining phases: **(a)** FBX e2e fixture = copy
assimp's vendored `box.fbx` (17 KB; no Blender locally, exporters disabled
in our build) with a provenance note; **(b)** clang+Ninja parity build
runs EARLY (before the Phase-2 bulk), not just pre-push — **DONE,
zero fixes needed** (§8); **(c)** milestone commits per green checkpoint
instead of one giant commit.

**Milestone 3 (session 2, detectors) — completed + validated:**

- **Latent Phase-1 bug fixed (§4.6):** `AssimpStagedIOSystem` normalized
  every request with base `""`, which breaks nested logical paths for OBJ
  (assimp opens the raw mtllib name) while glTF self-prefixes the model
  dir. Requests now resolve in two steps: base_dir-relative first (spec
  behavior), then already-qualified. Denials record the accurate name
  (qualified request → qualified name; raw request → model-relative name).
  Selftest grew to **85 checks** (nested model + raw mtllib + denial
  cases).
- `load_scene` gained an optional `error_out` param (defaulted; callers
  unchanged) so `--deep` failures carry Assimp's error into the report.
- Detector implementations (details in the source headers):
  - `detect-image.cc`: PNG/JPEG/GIF/BMP/PSD/HDR/PNM(P1–P6)/PIC magics +
    TGA (footer, then stb content fallback as the only remaining stb
    format); `stbi_info_from_memory` for header-only dims; manual
    PNG chunk walk (bit depth/color type/interlace + sRGB/gAMA), JPEG
    SOF marker walk (progressive), bounded GIF frame walk, manual PNM
    header parse for ALL variants (P1–P4 report dims too — small,
    deliberate deviation from the header comment's "e.g." which assumed
    stb-only parsing; P1–P4 get a warning + no actions).
  - `detect-model.cc`: binary FBX magic; GLB JSON-chunk scan **for both
    v1 (20-byte header) and v2 (chunk framing)** — assimp's vendored
    sample is v1; text heuristics (OBJ/MTL line-token scan over the whole
    file, glTF JSON heuristic, ASCII FBX) with dependency extraction
    (mtllib tokens, MTL last-non-dash token, glTF `"uri"` values incl.
    `data:` → embedded); Assimp `CanRead` sniff via `MemoryIOSystem`
    iterating importers exactly like ReadFile auto-detection, kind from
    `GetExtensionList` (never name-matching), FBX claims require a real
    FBX marker (CanRead's "fbx" substring token is too weak — a README
    mentioning fbx would otherwise be claimed).
  - `detect-shader.cc`: `@vertex/@fragment/@compute` scan with attribute
    -chain handling (`@compute @workgroup_size(...) fn name`), dedupe,
    runs only after model checks (most generic heuristic, per §5.3).
  - `deep-import.cc`: staged = `with_extra_entry` + `AssimpStagedIOSystem`
    (base_dir-aware) importing by LOGICAL name; direct =
    `AssimpRecordingIOSystem` on the physical path; mesh inventory +
    deterministic selection groups, materials + texture paths
    (`*`-prefixed → embedded), animations; journal opens minus the main
    file become deps (direct records get the physical dir prefix
    stripped, then normalized against base_dir).
  - `finalize_dependencies` (main.cc): journal deps are canonical names —
    never re-normalized (a re-normalize double-prefixed glTF requests);
  dedupe by normalized name merges sources (`gltf:uri, assimp:journal`)
  with journal status precedence; remote `scheme://` URIs are
  unresolvable (except `file:`, which normalize strips); direct-mode
  probes use the `error_code` overload of `exists` (the throwing
  overload TERMINATES on >NAME_MAX paths built from untrusted refs).
- `--deep` on MTL is a no-op note (assimp never imports MTL standalone);
  exit 3 verified with an OBJ assimp rejects; staged mismatch
  (logical name mapped to a different physical) → exit 1.
- Known behaviors (verified, by design): a glTF whose mapped buffer is
  missing fails BOTH importers' CanRead → "No suitable reader" error
  text (assimp's own) + exit 3 + light-scan deps; a Latin-1 MTL
  (assimp's `box_spaces.mtl`) correctly falls to `unknown` via the
  UTF-8 text gate; `box.obj` has no mtllib line (assimp finds the MTL by
  basename fallback), so its dep list is legitimately empty; the
  `copy-wgsl-source.wgsl` fixture is PLAINTEXT (no entry points) →
  correctly `unknown`; Phase-2 e2e will add a real WGSL fixture.
- Validation (all below on BOTH GCC and clang binaries): selftest
  **85/85**; both e2e suites **23/23** each; functional smokes: glass.png
  (dims + PNG details), real WGSL (2 entry points incl. chained
  attributes), OBJ/MTL/glTF/GLB(v1+v2)/binary FBX/ASCII PNM detection,
  deep direct glTF (mesh + merged `gltf:uri, assimp:journal` source
  dedupe), deep staged complete (in-manifest/resolved merge) and
  incomplete (unmapped merged from two denial records), deep FBX
  (mesh/material), progressive vs baseline JPEG, GIF frame counts,
  HDR/TGA(footer + fallback), 256-codepoint truncation (ASCII 256 bytes
  + multi-byte 512 bytes, valid UTF-8 after cut), remote URI handling,
  exit codes 0/1/2/3.

- Phase 0+1 is **committed on branch `extract-metadata-binary`** (branched
  from `main` @ `3d98eb7` = origin/main; not yet pushed — inventory in §9).
- Verified at handoff time and re-verified 2026-10-12: no stray/partial
  files from any aborted Phase 2 attempt exist anywhere in the tree.
- Baseline re-validated 2026-10-12 against the final binaries:
  `staged-asset-io-selftest` **67/67 checks**; igasset-gen e2e **23/23**;
  igpack-bundle e2e **23/23**; build tree current with sources. (The
  2026-10-11 numbers predated the final 22:32 rebuild by ~2.5 h — they
  remained valid, but were re-run to be sure.)
- **CI exists** (§8) and will compile this work with **clang** for the first
  time — all local validation so far is GCC-only. Run a local clang parity
  build before pushing (§10).
- Recommended first steps for a new session: §10.

## 1. Background — what this initiative is

The original request: add a new binary, **`extract-asset-metadata`**, to
`tools/` that takes a single input file (like `enumerate-igasset` /
`enumerate-igpack` do) and writes metadata about that file to stdout — what
kind of file it is, and **if and how `tools/igasset-gen` can consume it**
(which plan actions apply; e.g. a PNG is an image usable via
`ImageToTexture2DAction` or as input to `GenerateSpritesheetAction`). The
flagship feature added during planning: report **which other files the input
depends on** (an OBJ's `mtllib`, a glTF's `.bin` + textures, an FBX's texture
references) — the orchestrator uses this to decide processing and to pre-flight
its file set.

Deployment model that shaped the design (all user-confirmed):

1. An **orchestrating service** (outside this repo) receives user uploads,
   stores them in S3 under UUIDs, and pre-resolves user-facing **logical
   names** → S3 UUIDs.
2. Per job, the service **downloads** the needed objects into a per-job
   staging directory (files land with UUID names) and writes a **manifest**
   (`logical_path → physical file`) — see `schema/staged-asset-manifest.fbs`.
3. The service invokes these binaries inside a **Docker sandbox, offline**
   (`--network=none`). Binaries never talk to S3; every access resolves
   through the manifest; outputs are written to a local output root and the
   service uploads them back.
4. Common usage: run the tool against a whole *set* of files via one
   invocation per file (the service loops; per-file exit codes + containers).

Security context (drives several decisions): inputs may be malicious. Native
parser surface is the dominant risk (Assimp ≫ stb_image ≫ everything else),
resource-exhaustion DoS is trivial, and unsanitized plan paths + Assimp
external references are real arbitrary-read/exfiltration channels. The
manifest acts as a **deny-by-default capability list**: an input file can
never cause the tools to touch files outside the staged set. Sandbox
hardening (cgroup limits, wall-clock kills, fresh container per file,
non-root, read-only rootfs, gVisor consideration) is the **orchestrator's**
responsibility — summarized in §7.

## 2. Locked-in design decisions

Agreed with the user over seven planning rounds. **Do not relitigate without
explicit sign-off.**

- **D1 — Logical names end-to-end.** Plans and in-file references use logical
  names; the `staged-asset-io` facade + Assimp IOSystem adapters are the only
  place logical→physical mapping happens. The manifest is the single handoff
  contract between the orchestrator and the binaries.
- **D2 — Orchestrator owns S3 entirely.** Binaries are offline, local-only;
  zero AWS/S3 SDK deps ever. (An earlier "facade as a future direct-S3 seam"
  idea was explicitly dropped by the user.)
- **D3 — Two-policy facade.** `StagedFilesystem::direct()` reproduces
  historical igasset-gen behavior byte-for-byte (plain `operator/` root
  joining, no containment checks — trusted files; preserves backwards
  compat for existing local tooling the user has). `StagedFilesystem::
  staged(manifest)` is deny-by-default for untrusted uploads. CLI rule across
  all tools: **`--manifest` present → staged; absent → direct.**
- **D4 — Assimp importer set narrowed to FBX + glTF 1/2 + OBJ** (already
  applied, §4.3) and Assimp bumped **v6.0.4 → v6.0.5** (security-supported).
- **D5 — Image formats = the stb_image set:** PNG, JPEG, TGA, BMP, PSD, GIF,
  HDR, PIC, binary PNM. Consumed via `ImageToTexture2DAction` and
  `GenerateSpritesheetAction` (see `schema/igasset-gen-plan.fbs`).
- **D6 — One input file per invocation** of extract-asset-metadata; the
  orchestrator loops. No batch mode in v1.
- **D7 — Dependency extraction is a flagship feature:** logical-name
  references with source + status; names truncated to **256 code points**
  (UTF-8-boundary-safe, `truncated` flag; status/source never truncated).
- **D8 — Content-first detection, decode-free by default.** Physical names
  are UUIDs (extensions useless). Magic bytes → flatbuffer probes → text
  heuristics → Assimp `CanRead` sniffing. Default mode parses headers only
  (`stbi_info_from_memory`, never full decode). Full Assimp import only
  behind opt-in `--deep`.
- **D9 — Sync I/O** (Phase 0, done). igasset-gen keeps its **exec thread
  pool** for CPU work (encode/decode); only file I/O came off the async bus.
- **D10 — Output:** human-readable text by default (enumerate-* style),
  `--json` for machines. No JSON library — hand-rolled minimal printer with
  strict escaping.
- **D11 — Existence-oracle ban in staged mode:** dependency status resolves
  via pure manifest lookup, never filesystem probing. Direct mode may probe.
- **D12 — extract-asset-metadata writes nothing to disk** (stdout only) and
  needs no thread pools.
- **D13 — WGSL:** no WGSL parser exists in-repo; heuristics only (entry-point
  scan). Build-time risk for WGSL is zero (verbatim copy via
  `CopyWgslSourceAction`).
- **D14 — Direct mode is v1 backwards compat only.** The user's preference
  for keeping it "is not strong"; all policy lives exclusively in the facade
  so direct can be retired later with a one-line change per consumer.

## 3. Phase 0 — Sync I/O swap [COMPLETE]

**Why:** the async I/O facade (built for a hypothetical network filesystem)
made Assimp/basisu integration painful; the network-FS requirement was
dropped by the user. What changed (all validated before Phase 1 began):

- `utils/tool-utils/include/tool-utils/filesystem.h` + `src/filesystem.cc`:
  `toolutils::Filesystem` is now a **synchronous** facade — `read_bin(path)
  → std::optional<std::string>` and `write_bin(path, FlatBufferBuilder&)
  → bool`. Deleted: `write_bin_async` (had zero callers), the `io_task_list`
  ctor param, `enable_shared_from_this`, both igasync includes. `igasync`
  removed from tool-utils' PUBLIC link list (enumerate-* binaries no longer
  transitively link it).
- `tools/igasset-gen/main.cc`: io task list + 2-thread io pool deleted;
  **exec task list + exec pool (hardware concurrency) retained**;
  `--single-threaded` unchanged in meaning; spin loop is now simply
  `while (!igassets_finished) { exec_task_list->execute_next(); }`.
- Processors straightened to single exec tasks:
  - wgsl-copy: one exec task (cache check → read → validate → build → write)
  - assimp-geo: one exec task driving a local `process` lambda (cache check →
    read → import → Draco → build → write)
  - basisu: `generate_single_output` is now a sync `bool` (resize → encode →
    write inline); `ImageToTexture2D` = one exec task for read+decode, then
    **per-output exec tasks via the PromiseCombiner** (parallel compression
    across outputs/actions preserved — and now genuinely parallel; the old
    code compressed serially); spritesheet = one exec task with a plain
    for-loop blit chain (the old serial `then_chain` web collapsed)
- `tools/igpack-bundle/`: constructs `toolutils::Filesystem(log)` (it used to
  pass a null task list); no longer links igasync.
- igasset-gen still links igasync **directly** (CPU parallelism) — intentional.

Validation at completion: full build green; igasset-gen e2e 23/23;
igpack-bundle e2e 23/23; multi-threaded multi-output smoke run OK.
Net −199 lines across 14 files (Phase 0 portion).

## 4. Phase 1 — `staged-asset-io` foundation [COMPLETE]

### 4.1 New files

- `schema/staged-asset-manifest.fbs` — flatbuffers schema, namespace
  `IgAssetStaging`. `StagedAssetManifestEntry { logical_path: string
  (required); physical_path: string (required); s3_uuid: string (optional,
  traceability only — never used for I/O) }`, root `StagedAssetManifest`.
  Authored by the orchestrator as JSON.
- `tools/staged-asset-io/` — static lib **`staged-asset-io`** (C++23) plus a
  **`staged-asset-io-selftest`** binary. Registered **first** in
  `tools/CMakeLists.txt` (other tools may link it).

### 4.2 Library API surface (`tools/staged-asset-io/include/staged-asset-io/`)

| Component | API | Semantics |
|---|---|---|
| `reference-normalize.h` | `logical_parent_dir(sv)`; `normalize_logical_reference(base_dir, ref) → optional<string>` | Pure-string normalization: strips `file://`, `\`→`/`, percent-decodes, resolves `.`/`..` lexically against `base_dir`; rejects empty / leading-`/` / Windows-drive / escaping refs. **Gotcha:** one `..` from `"models/"` yields `"escape.png"` — still inside the logical namespace; only popping past the root is rejected (correct by design). Never touches the filesystem. |
| `file-access-journal.h` | `FileAccessJournal::record(path, allowed)` / `snapshot()` | Thread-safe append-only log of every **input** access (allowed + denied). **Shared ownership by design:** it must outlive the `Assimp::Importer`, which deletes its IOSystem. |
| `staged-asset-manifest.h` | `StagedAssetManifest::from_json/load → shared_ptr<const …>`, `physical_for(normalized_key) → const string*`, `with_extra_entry(logical, physical, log) → shared_ptr<const …>` | Parsed via the repo's embedded-schema machinery (`embed_fbs_schema` + `toolutils::ParseEmbeddedSchema`). Load-time validation: logical paths must normalize (else reject); physical names must be **bare filenames** (no dirs, no `..`, no absolute); duplicates rejected. Keys stored **normalized** — callers must normalize before `physical_for`. Immutable after load (concurrent action tasks need no locking). `with_extra_entry` returns a **copy** with one validated runtime mapping (synthesized input entry for tools whose input isn't manifest-mapped); identical duplicate tolerated, conflicting mapping → nullptr; original never modified. |
| `staged-filesystem.h` | `StagedFilesystem::direct(in, out, log)`; `::staged(in, out, manifest, log)`; `resolve_input/resolve_output`; `read_input`; `write_output`; `journal()`; `manifest()`; `policy()` | The two-policy facade (D3). Staged `resolve_input` = normalize → manifest lookup → `input_root / physical`; invalid/unmapped → nullopt + journal denial + warn log (never probes disk). Staged `resolve_output` = normalize (rejects `..`/absolute) → `output_root / logical`. Direct = historical `operator/` semantics (absolute RHS replaces root) — intentionally unchecked, trusted input only. Composes `toolutils::Filesystem` for actual disk I/O. Output writes are **not** journaled. `staged()` with null manifest returns nullptr. |
| `assimp-io-system.h/.cc` | `AssimpStagedIOSystem`; `StagedIOStream`; `AssimpRecordingIOSystem` | Staged: every `Open`/`Exists` resolves through the manifest; unmapped → deny + record, no disk probing; `Open` rejects write modes (inputs read-only); `ComparePaths` compares normalized logical names. Recording: composes a stock `Assimp::DefaultIOSystem` (it's `final` but public — delegation, not subclassing), records every open, otherwise behavior-identical to the default IOSystem (for direct mode dependency extraction). `StagedIOStream`: `std::ifstream`-backed `Assimp::IOStream`; the stream member is **`mutable`** (assimp's `Tell()` is const); `Seek(aiOrigin_END)` **negates** the offset (assimp passes positive). |
| `assimp-import.h/.cc` | `igasset_import_flags()`; `load_scene(path, igasset_name, log, custom_io = nullptr) → std::shared_ptr<AssimpSceneData>` | Returns a holder (`shared_ptr<Importer>` + raw `const aiScene*` — keep the holder alive while reading the scene). The single shared Assimp entry point (moved out of igasset-gen's now-deleted `assimp-util.{h,cc}`). Flags: `aiProcessPreset_TargetRealtime_Quality \| aiProcess_FlipUVs \| aiProcess_CalcTangentSpace \| aiProcess_FixInfacingNormals`; property `AI_CONFIG_PP_LBW_MAX_WEIGHTS = 4`. Uses `Importer::ReadFile` (path-based, not from-memory) so external references resolve through the installed IOSystem. **`Importer` owns and deletes `custom_io`** — pass shared state (journal/manifest) as shared_ptr members, not IOSystem state. |

### 4.3 Build changes (`extern/CMakeLists.txt`)

- Assimp tag **v6.0.5** (verified post-build: `GitVersion 0x392a658f` matches
  the `v6.0.5` tag SHA).
- `ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT` → **OFF**; only
  `ASSIMP_BUILD_FBX_IMPORTER`, `ASSIMP_BUILD_GLTF_IMPORTER`,
  `ASSIMP_BUILD_OBJ_IMPORTER` ON. **The GLTF importer module covers both
  glTF 1 and glTF 2** (one CMake option, two importers registered).
- Verified: 66 `ASSIMP_BUILD_NO_*` defines active; only FBX / glTF / glTF2 /
  glTFCommon / Obj AssetLib objects freshly compiled. (The assimp build dir
  also contains *stale* pre-narrowing object dirs — trust `flags.make` defines
  or wipe `build/_deps/assimp-build` for a pristine check.)
- igasset-gen now links `staged-asset-io` (PRIVATE) and its
  `assimp-geo-processor.cc` calls `stagedio::load_scene` — behavior-identical
  (same flags as the old local copy by construction).

### 4.4 Validation (all green at handoff)

- `build/tools/staged-asset-io/staged-asset-io-selftest` — **67 checks,
  ALL PASS.** Coverage:
  - normalization accept/reject cases (incl. percent-decoding, backslashes,
    `file://`, drive-letter and `..`-escape rejection)
  - manifest validation (bad JSON, absolute/traversal logical paths,
    subdirectory/traversal physical names, duplicates, missing required
    fields — all rejected)
  - direct policy: historical join semantics incl. the
    absolute-path-overrides-root behavior; read/write round-trip
  - staged policy: mapped read resolves to the UUID file; unmapped /
    `../` traversal / absolute reads denied; journal records both denials
    and allowed reads; contained output write OK; escaping + absolute
    writes denied
  - **the flagship:** Assimp imports an OBJ **by logical name** through a
    manifest that maps to UUID-named staged files; the referenced MTL's
    *content* is proven to flow through the custom IOSystem (its
    `Kd 1 0 0` shows up as the material diffuse color); the denial case is
    proven (manifest without the MTL → OBJ importer proceeds with a
    placeholder material that keeps the default diffuse `(0.6,0.6,0.6)` +
    a denied journal record; zero disk reads); and the journal is read
    **after** the Importer destroyed the IOSystem (the shared-ownership
    pattern works).
- igasset-gen e2e **23 passed**; igpack-bundle e2e **23 passed** — the
  regression gate for the `assimp-import` move + the 6.0.5 rebuild.
- Post-clang-format rebuild green.

### 4.5 Phase-1 security audit findings (keep for the record)

- **IOSystem-bypass audit of FBX / glTF / glTF2 / OBJ on v6.0.5 source:
  clean.** The only `fopen` sites (`glTFAsset.inl` ~line 1280,
  `glTF2Asset.inl` ~line 2219) are inside the `#else` of
  `#ifdef ASSIMP_API` — dead code in our build; the live path is
  `mIOSystem->Open(...)`. OBJ's `DefaultIOSystem()` uses are
  `getOsSeparator()` constants only. All three importers route external
  references through the installed IOSystem.
- Residual: `IOSystem::CreateDirectory/ChangeDirectory/DeleteFile` are
  non-virtual inline helpers (not overridable) — irrelevant; the audited
  importers never call them.
- `Importer::ReadFile` selects importers by the **file extension of the
  path passed** → staged mode must pass the *logical* name to `ReadFile`;
  the IOSystem maps it to the UUID physical file. This is why
  `--logical-name` exists in the extract-asset-metadata CLI (§5.2).

### 4.6 Two-step request resolution in AssimpStagedIOSystem (added 2026-10-12, milestone 3)

Assimp importers disagree on reference qualification: **OBJ opens the raw
mtllib token** (`absName = strMatName` when the IOSystem directory stack is
empty — ObjFileParser.cpp), then falls back to `<model-base-name>.mtl`
(the ReadFile path with the extension swapped); **glTF 1/2 prepend the
model's own directory themselves** (`mCurrentAssetDir` derived from the
ReadFile path, `OpenFile(dir + uri)`). A single-step normalize therefore
cannot serve both. Resolution order (first manifest hit wins): (1)
model-relative — `normalize(base_dir, request)`; (2) already-qualified —
`normalize("", request)`. Denials record the name matching the request
form: a request already carrying the base_dir prefix is recorded
qualified; a raw reference is recorded model-relative (where the
orchestrator must map it). `Exists` mirrors both steps; the ctor takes
`base_dir` (defaulted `""`). Selftest coverage: 85 checks incl. nested
`sub/scene.obj` + raw `mtllib scene.mtl` → resolved as `sub/scene.mtl`,
and the OBJ-fallback denial recording both forms under one name.

## 5. Phase 2 — `tools/extract-asset-metadata` [COMPLETE, VALIDATED]

Everything in §5 is implemented and validated on GCC + clang. The e2e
suite lives in `tools/extract-asset-metadata/e2e/` (31 tests); the CI
workflow builds the tool + the staged-asset-io selftest and runs the
suite (§5.9). Open questions that were collected in §5.11 are all
resolved.

### 5.1 Purpose

Inspect one file; report: kind, category, key metadata, which igasset-gen
actions can consume it and with what parameters, and the file's
dependencies by logical name. Consumed by the orchestrator (a) to decide how
to process each upload and (b) to pre-flight manifests before spending an
igasset-gen job.

### 5.2 CLI contract

```
extract-asset-metadata [--json] [--deep] [--manifest <manifest.json>]
                       [--logical-name <name>] [--max-file-size <bytes>]
                       <physical_input_file>
```

- `<physical_input_file>` — required positional, `CLI::ExistingFile` check
  (pattern: `tools/enumerate-igasset/main.cc`).
- `--manifest` — present → staged policy; absent → direct policy (D3).
  Loaded once, immutable for process lifetime.
- `--logical-name` — the logical name of the input. **Required in practice
  for staged mode:** UUID filenames defeat (a) manifest lookup and (b)
  Assimp's extension-based importer selection. Default: the physical file's
  own filename. Staged-mode contract: the physical file is expected to live
  in the staging root (its manifest `physical_path`); if the logical name is
  not in the manifest, synthesize a runtime entry
  (`logical_name → filename of the physical input`) so `--deep` can import
  via the IOSystem. Document the staging-root expectation in `--help`.
- `--json` — machine-readable output (default: text).
- `--deep` — opt-in full Assimp import = the entire Assimp attack surface;
  document as such in `--help`.
- `--max-file-size` — refuse larger files **before reading** (default:
  1 GiB — adopted 2026-10-12, §5.11).

### 5.3 Detection order (content-first; cheap → expensive)

1. **Image magic bytes**: PNG/JPEG/GIF/BMP/PSD/HDR/PNM(P1–P6)/PIC signatures;
   TGA via `TRUEVISION-XFILE` footer or stb fallback. On hit →
   `stbi_info_from_memory` (header-only: width/height/channels — never
   decode) + targeted header details: PNG IHDR bit depth / color type /
   interlace + `sRGB`/`gAMA` chunk presence; JPEG progressive; GIF frame
   count.
2. **Flatbuffer probes**: `IgAsset::VerifyAssetBuffer` /
   `VerifyAssetPackBuffer` (generate schema targets for `types.fbs`,
   `igasset.fbs`, `igpack.fbs` via `gen_flatbuffer_cpp`, following
   `tools/enumerate-igpack/CMakeLists.txt`).
3. **Text heuristics** (only if plausibly text: no NUL in the first ~4 KiB
   and valid UTF-8): OBJ (`mtllib`/`usemtl`/`v `/`f ` lines), MTL
   (`newmtl`, `map_*`), glTF (JSON with `"asset"` + `"scenes"`/`"scene"`
   keys), FBX-ASCII (`; FBX` / `FBXHeaderExtension` header), WGSL
   (`@vertex`/`@fragment`/`@compute` fn scan). Check OBJ/MTL/glTF/FBX
   patterns before WGSL (WGSL is the most generic).
4. **Assimp sniff**: binary FBX magic (`Kaydara FBX Binary`), GLB magic
   (`glTF`), else `Importer::CanRead(path, MemoryIOSystem(bytes), true)`
   over the loaded bytes; identify via
   `GetImporter(GetImporterIndex(path))->GetInfo()->mName` (only 3
   importers exist in the build now).
5. **Unknown** → still a successful inspection; report `kind: unknown`.

Kinds: `png/jpeg/tga/bmp/psd/gif/hdr/pic/pnm` (category `image`),
`fbx/gltf/glb/obj/mtl` (`model`; `mtl` = dependency kind),
`wgsl` (`shader`), `igasset`/`igpack` (`generated`), `unknown`.

### 5.4 Per-kind metadata + supported igasset-gen actions

- **image** → actions: `ImageToTexture2DAction` (mention `Output2DImage`
  fields: encoding, explicit w/h, quality, use_srgb, gen_mips) +
  `GenerateSpritesheetAction` (as sprite input; crop fields available).
  Metadata: width, height, channels, bit depth (PNG). **Pipeline caveats to
  report:** >8-bit sources (16-bit PNG/PSD, HDR float) are silently
  reduced to 8-bit; **resize only works for 3/4-channel images**
  (stb-resize path in `igasset-gen` `stb-parse.{h,cc}`); 1/2-channel
  inputs + RGBA8 output currently hit the OOB bug until Phase 3 §6.5
  lands (report a warning).
- **model** → action: `AssimpToDracoAction`. Mesh selection is **by name**
  (`assimp_mesh_names`); `include_texcoords`/`include_bones` must be
  uniform across the selected meshes (processor validates). Light-mode
  metadata: format, declared dependencies (§5.5). `--deep` metadata:
  importer name, mesh inventory (name / vertices / faces / has-texcoords /
  bones), texcoord+bone consistency groups (which mesh names can be
  selected together), animation count, materials + texture paths.
  Binary-FBX light-scan is limited — `--deep` is authoritative there.
- **wgsl** → action: `CopyWgslSourceAction` (verbatim copy; detected entry
  point names map to the action's `vertex/fragment/compute_entry_point`
  fields). Metadata: entry points, size; empty-file warning.
- **igasset / igpack** → not igasset-gen *inputs*; point the caller at
  `enumerate-igasset` / `enumerate-igpack` / `igpack-bundle`.

### 5.5 Dependencies section (the flagship)

Scan for referenced **logical names** and resolve against the input's
logical parent dir using the shared `normalize_logical_reference` +
`logical_parent_dir` from staged-asset-io (one implementation for scanners
and IOSystem). Sources:

- OBJ: `mtllib <names…>` (multiple per line, multiple lines);
  MTL: `map_Kd/map_Ka/map_Ks/map_d/map_bump/bump` texture paths.
- glTF (text JSON): `"uri"` values in `buffers` and `images`;
  `data:` URIs → status `embedded` (not a dependency); GLB: usually
  embedded, but surface any external `uri` it references.
- FBX (deep mode): `aiGetMaterialTexture` / `AI_MATKEY_TEXTURE_*` paths.
- Deep mode generally: **journal opens** (everything Assimp opened, minus
  the main file) ∪ material-texture paths.

Status resolution per D11:
- **staged mode**: `in-manifest` / `not-in-manifest` via **pure manifest
  lookup** (never probe disk — `not-in-manifest` is exactly what the
  orchestrator needs to fix its manifest). With `--deep`, add
  `resolved` (journal, read succeeded) / `unmapped` (journal denial).
- **direct mode**: `resolved` / `missing` via filesystem existence check
  (allowed in trusted mode); `embedded` where applicable.

Names: truncate **display** to 256 Unicode code points on UTF-8
boundaries; add `truncated: true`. Never truncate status/source fields.

### 5.6 Output

- Text default, styled like `enumerate-igasset`
  (`------- Asset Metadata -------`; `tool-utils/stringify.h` where useful).
- `--json`: one JSON object per run; the schema is the **union of both
  modes**, and every run reports `mode: direct|staged`. Hand-rolled printer
  — **no JSON library dependency**; escape `"` `\` and control chars; strip
  invalid UTF-8 on echo (untrusted bytes flow into the output).

### 5.7 Security constraints (baked in from the review)

- Size cap before any read (§5.2); compute SHA-256 streaming/chunked
  (picosha2 over chunks — do not unconditionally slurp into RAM).
- Default mode is decode-free (D8); `--deep` is opt-in and documented as
  the full Assimp surface.
- No existence oracle in staged mode (D11).
- Sanitize every echoed string (§5.6).
- Writes nothing to disk (D12) — stdout only.

### 5.8 Exit codes (distinct, for orchestrator policy)

| Code | Meaning |
|---|---|
| 0 | Inspected OK — including `kind: unknown` (a successful inspection) |
| 1 | Bad args / unreadable file |
| 2 | File exceeds `--max-file-size` |
| 3 | `--deep` parse failure (detected as a model but Assimp rejected it) |

(If the orchestrator wants unknown-kind as a distinct signal, prefer parsing
the `kind` field of `--json` output over adding an exit code; see §5.11.)

### 5.9 Build/CMake

Model on `tools/enumerate-igpack/CMakeLists.txt`: `gen_flatbuffer_cpp` for
`types.fbs`, `igasset.fbs`, `igpack.fbs` (BIN_PATH_NAME
`extract-asset-metadata/schema`); link `CLI11::CLI11 spdlog::spdlog
picosha2 tool-utils staged-asset-io assimp stb` + schema targets. C++23.
Register `add_subdirectory(extract-asset-metadata)` in `tools/CMakeLists.txt`
(after `staged-asset-io`, which is listed first).

Suggested layout (mirrors igasset-gen's multi-file shape):
`main.cc`; `src/detect-image.cc`; `src/detect-model.cc` (text scans +
assimp sniff/deep); `src/detect-shader.cc`; `src/report-text.cc`;
`src/report-json.cc`; shared internal header(s) under
`include/extract-asset-metadata/`. Build one internal `AssetMetadata`
struct (kind, category, per-kind payload, actions list, dependencies list,
mode, truncated names) and render it with either printer.

**CI (see §8):** `.forgejo/workflows/igasset-gen-integration.yaml` builds
the gen tools and runs the pytest suites on every push to `main` / PR. When
this tool lands, update that workflow: (a) add
`--target extract-asset-metadata` to the Build step, (b) add a pytest step
for `tools/extract-asset-metadata/e2e/` with env
`EXTRACT_ASSET_METADATA_BIN=build/tools/extract-asset-metadata/extract-asset-metadata`.
CI does not currently build or run `staged-asset-io-selftest` — add it to
the Build step too (fast; guards the facade).

### 5.10 Tests + fixtures (pytest e2e, per repo conventions)

conftest modeled on `tools/igasset-gen/e2e/conftest.py` (which defines the
pattern: `pytest_addoption` + `--<name>-bin` options with env-var fallback
`IGASSET_GEN_BIN`, `ENUMERATE_IGASSET_BIN`, and `--asset-root` /
`IGASSET_TEST_ASSET_ROOT` defaulting to the repo's `test_assets/`
**[correction 2026-10-12: the CLI option is `--asset-root`, not
`--igasset-test-asset-root`; the env var was right]**): add
`--extract-asset-metadata-bin` (env `EXTRACT_ASSET_METADATA_BIN`) + an
`asset-root` fixture; include `pytest.ini` + `requirements.txt` like the
existing suites.

Cases:
1. Direct mode over existing `test_assets/`: `glass.png` → image/png +
   correct dims/channels; `copy-wgsl-source.wgsl` → shader + entry points;
   `sprite_*.png` → image.
2. Staged mode: copy fixtures to UUID names in a tmp staging dir + write a
   manifest; run with `--manifest --logical-name`; kind/metadata identical
   to direct mode.
3. Dependencies: OBJ fixture's `mtllib` → mtl dependency; glTF fixture →
   `.bin` + texture dependencies; staged mode with a deliberately
   incomplete manifest → `not-in-manifest`; complete manifest →
   `in-manifest`.
4. `--json` parses (e.g. `json.loads`) and round-trips kind/width.
5. Random-bytes file → `kind: unknown`, exit 0.
6. Oversized file → exit 2 (tiny `--max-file-size`).
7. `--deep` over the OBJ fixture: 1 mesh with the right name; missing-mtl
   dependency listed with the right status.

**Fixtures (landed in `test_assets/`, milestone 4):** hand-authored
`triangle.obj` + `triangle.mtl` (mtllib + map_Kd deps; `o triangle` gives
the deep import a stable mesh name), `scene.gltf` + `scene.bin` (36-byte
triangle buffer) + `tex.png` (120-byte generated 4x4 RGB PNG — shared by
the glTF and the MTL), `entry-points.wgsl` (real WGSL with @vertex/
@fragment entry points), and `box.fbx` **copied from assimp's vendored
test suite** (`build/_deps/assimp-src/test/models/FBX/box.fbx`, 17 KB —
no Blender locally and exporters are disabled in our build; provenance
in `test_assets/README.md`). **Spec deviation:** §5.10 originally
expected `copy-wgsl-source.wgsl` to yield "shader + entry points", but
that fixture is plaintext with no entry points (it correctly reports
`kind: unknown`); the WGSL entry-point test uses the new
`entry-points.wgsl` instead, and the existing fixture is untouched
(zero-fixture-change invariant).

**e2e suite (31 tests, `tools/extract-asset-metadata/e2e/`):** conftest
mirrors the igasset-gen pattern (`--extract-asset-metadata-bin` /
`EXTRACT_ASSET_METADATA_BIN`, `--asset-root` / `IGASSET_TEST_ASSET_ROOT`);
`test_base.py` adds a `StagingDir` helper (UUID copies + manifest). Cases:
direct detection (png/tex/sprite/wgsl/obj/mtl/gltf/fbx/unknown), JSON
round-trip + sha256 vs hashlib + text/JSON agreement + omitted per-kind
sections, staged mode (identical metadata to direct, in-manifest /
not-in-manifest, NESTED logical names proving the two-step resolution),
--deep (direct obj/gltf/fbx inventories + merged journal sources; staged
complete/unmapped; exit-3 for staged glTF missing its buffer and for a
corrupt OBJ; MTL no-op note), and the full exit-code contract (0/1/2/3 +
help). One more fix found by the suite: **`Importer::GetImporterIndex`
takes the BARE extension ("fbx"), not a path** — it compares its argument
verbatim against each importer's extension list, so deep reports were
silently missing `importer_name` when passed a full path.

**CI (milestone 4):** `igasset-gen-integration.yaml` now builds
`extract-asset-metadata` + `staged-asset-io-selftest`, runs the selftest
as a step, and runs the new pytest suite (env
`EXTRACT_ASSET_METADATA_BIN`). Local clang parity re-verified after the
workflow edit; first CI run after push re-fetches deps (cache-key
invalidation, expected — §8).

### 5.11 Open questions — RESOLVED 2026-10-12 (defaults adopted with the
user's plan approval)

1. Default `--max-file-size` = **1 GiB**. SHA-256 is **always computed**,
   streaming/chunked (bounded by the size cap) — the orchestrator wants
   content identity; revisit with an opt-out flag only if the cost bites.
2. `kind: unknown` → **exit 0**; the orchestrator parses the JSON `kind`
   field (no additional exit code).
3. `--deep` does **not** decode images — image analysis stays header-only
   even in deep mode; deep = models only.

## 6. Phase 3 — igasset-gen staged rewiring [NOT STARTED]

Goal: igasset-gen accepts `--manifest <file>` (staged policy); **without it,
behavior must remain byte-identical to today** (direct policy — that is the
whole point of D3/D14). Plans become purely logical-name based.

### 6.1 CLI (main.cc)

- Add `--manifest` (`CLI::ExistingFile`). Absent →
  `stagedio::StagedFilesystem::direct(input_root, output_root, log)`.
  Present → load manifest →
  `StagedFilesystem::staged(input_root, output_root, manifest, log)`
  (input root = staging dir; output root = the job's output dir).
- Construct the `StagedFilesystem` before creating processors and thread it
  through `PlanInvocationConfig` — replace the `InputAssetPathRoot` /
  `IgassetPathRoot` fields with
  `std::shared_ptr<stagedio::StagedFilesystem> staged_fs`. Current shape of
  `tools/igasset-gen/include/igasset-gen/exec-config.h`:

```cpp
struct PlanInvocationConfig {
  std::filesystem::file_time_type PlanFileLastModified;
  std::filesystem::path InputAssetPathRoot;
  std::filesystem::path IgassetPathRoot;
  bool CleanBuild;
};
```

### 6.2 Processor changes (all three)

They currently do
`config_.InputAssetPathRoot / action->input_file_path()->str()` +
`filesystem_->read_bin(...)` (and writes
`filesystem_->write_bin(config_.IgassetPathRoot / out, fbb)`). Replace with
`staged_fs->read_input(logical)` / `staged_fs->write_output(logical, fbb)`.
Log with **logical** names (better service logs). Drop the now-redundant
`toolutils::Filesystem` member (the facade composes it internally).

### 6.3 Assimp path (assimp-geo-processor.cc)

- Staged: install
  `new AssimpStagedIOSystem(input_root, manifest, journal, log)` and pass
  the **logical** input name (with its original extension!) to
  `stagedio::load_scene` — `ReadFile` needs the extension for importer
  selection; the IOSystem maps it to the UUID file.
- Direct: install `AssimpRecordingIOSystem` (behavior-identical + free
  dependency journaling), or nothing (simplest) — pick one and note it.
- Ownership reminder: `SetIOHandler` **transfers ownership** (Importer
  deletes the IOSystem). Keep journal/manifest as processor-held
  `shared_ptr`s (that's the working pattern proven by the selftest).

### 6.4 inc-build (mtime cache) interactions

`src/inc-build.cc` caches outputs keyed on plan mtime + input mtimes.
- Staged mode: input mtimes = physical staged-file mtimes (fine — same
  object identity per job).
- After rewiring, verify the cache records store **logical** output names
  consistently (direct↔staged runs on a shared tree would otherwise thrash
  the cache — mostly moot in fresh per-job containers, but keep it clean).
- Existing TODOs mark missing cache checks on the image paths; do **not**
  regress what exists, but adding new caching is out of scope.

### 6.5 Correctness fixes (unconditional — not policy-gated)

1. **OOB read in `rgba8_unorm_out`** — `tools/igasset-gen/src/
   basisu-processor.cc` lines ~429–464. For images with
   `num_channels < 4`, the conversion loop reads `pixel_ptr[0..2]`
   **unconditionally** (pointer stride = `num_channels`), so 1-channel
   (grayscale) and 2-channel inputs read past the end of the decoded heap
   buffer; the over-read bytes are copied into the output `.igasset`
   (heap info-leak oracle when the output is served back, plus a plain
   correctness bug). Fix: read only valid channels — 1-ch → `(g,g,g,255)`,
   2-ch → `(g,g,g,a)` (confirm stb's 2-channel semantics when fixing).
   **`basisu_ldr_out` (same file, lines ~517–524) has the identical
   unconditional `pixel_ptr[0..2]` read — fix both sites.** The
   spritesheet path is safe (sprites are force-loaded 4-channel:
   `StbImageData::from_memory(*rsl, 4)`).
2. **`int` byte-count arithmetic**: `img.width * img.height *
   img.num_channels` feeding `CreateVector` (~line 438) can overflow
   int32 for huge images — use `size_t`/`uint64_t` for byte counts
   throughout.
3. **`uint16_t` loop counters in `rgba8_unorm_out`** (basisu-processor.cc
   ~lines 443–444; found in the 2026-10-12 audit): the conversion loops use
   `uint16_t row/col` against `int` image dims. A source image with a
   dimension > 65,535 px (stb permits up to `1<<24`) wraps the counter, so
   the loop never terminates and `converted_data` grows without bound (DoS).
   Use `int`/`size_t` counters. The 4-channel fast path (~line 436) is
   unaffected.
4. Optional: watch Assimp upstream for releases newer than 6.0.5 (the
   currently security-supported version).

### 6.6 Phase 3 tests

- **Existing e2e suites must stay green with zero fixture changes** — that
  is the direct-mode invariant (D3).
- Add one staged-mode e2e: UUID-copies of `test_assets/` + the new glTF
  fixture (from Phase 2 §5.10); run a manifest-backed plan containing a
  `CopyWgslSourceAction` and an `AssimpToDracoAction`; verify `.igasset`
  outputs via `enumerate-igasset` (`DracoGeometry` present, mesh count 1).
- Add a regression test for the OOB fix: grayscale (1-channel) PNG →
  `ImageToTexture2DAction` RGBA8 output with explicit dims → output byte
  length `== w*h*4` (and pixel values `(g,g,g,255)` if practical).

## 7. Follow-ups & explicitly out of scope

- **Docker hardening — orchestrator's side of the contract:**
  `--network=none`, non-root user, `--read-only` rootfs + tmpfs `/tmp`,
  `--cap-drop=ALL --security-opt=no-new-privileges`, cgroup limits
  (`--memory=2g --cpus=2 --pids-limit=64`), `ulimit -f` output-size cap,
  wall-clock kills (~30 s metadata / ~300 s gen), **fresh container per
  file**, consider gVisor/Firecracker for stronger isolation; upload
  staging size caps + symlink rejection before any binary runs.
- `IGASSET_REQUIRE_MANIFEST=1` env guard in igasset-gen (hard-fail when
  `--manifest` is absent) — optional service-misconfiguration tripwire;
  agreed: noted, **not v1-blocking**.
- Hash-based (content-hash) build caching — **correction (2026-10-12): it
  is NOT on the README roadmap** (the roadmap lists metadata sidecars,
  asset viewers, memory limits, runtime profiling). The live cache TODOs
  are the two in `basisu-processor.cc` (~lines 167 and 264 — missing cache
  checks on the image paths; the same ones §6.4 says not to regress).
  mtime caching is moot per-job in fresh containers — don't block.
- Direct-mode retirement path (D14): policy lives only in the facade, so
  retiring direct later is a one-line change per consumer.
- `enumerate-*` and `igpack-bundle` stay untouched (beyond Phase 0's facade
  change). Note: `enumerate-assimp` will now report only the 3 compiled
  importers — informational only.
- extract-asset-metadata batch mode: deliberately deferred (orchestrator
  loops per file; per-file exit codes + containers).
- Whole-file-in-RAM reads remain (facade is the future seam if streaming is
  ever needed) — accepted for v1.
- **Forgejo issue #18** (open, 2026-09-15; unrelated to this pipeline —
  noted so it isn't lost): decoder-side bug — unpacking an RGBA8Unorm-stored
  Image2D into BC7/ASTC always fails (`IgpackDecoder::extract_image2d`,
  `SingleAssetParser::extract_image2d`, `Image2D::Unpack`). Runtime-library
  scope, not generator tooling; track separately.

## 8. Appendix — key technical facts (learned the hard way)

**Assimp (v6.0.5, FetchContent, patched for llvm-rc via
`extern/patches/assimp-fix-llvm-rc.cmake`):**
- `Importer::SetIOHandler(IOSystem*)` **takes ownership**; the Importer
  deletes the IOSystem on destruction/replacement. Shared journal/manifest
  state (shared_ptr) is the working pattern.
- `Importer::ReadFile` selects importers by the **extension of the path
  passed** — pass logical names in staged mode.
- Sniffing: `Importer::CanRead(path, MemoryIOSystem*, true)` +
  `GetImporterIndex(path)` / `GetImporter(i)->GetInfo()->mName`;
  `assimp/MemoryIOWrapper.h` is public; magic filename
  `AI_MEMORYIO_MAGIC_FILENAME`.
- `IOSystem` pure virtuals: `Exists`, `getOsSeparator`, `Open`, `Close`
  (directory-stack fns have defaults; `CreateDirectory/ChangeDirectory/
  DeleteFile` are non-virtual inline — not overridable, unused by our
  importers). `IOStream` pure virtuals: `Read`, `Write`, `Seek`, `Tell`
  (**const** — use a `mutable` stream), `FileSize`, `Flush`.
- Return codes are the enum values `aiReturn_SUCCESS` / `aiReturn_FAILURE`
  (**no** `AI_RETURN_*` macros exist). `aiGetMaterialColor` takes
  `aiColor4D`. `IOStream::Seek(aiOrigin_END)` receives a **positive**
  offset (negate for `ifstream`).
- OBJ importer quirk: a missing MTL still produces a material **named after
  the `usemtl` request** but with default diffuse `(0.6, 0.6, 0.6)` —
  distinguish by content, not by name.
- Importer CMake options: `ASSIMP_BUILD_{FBX,GLTF,OBJ}_IMPORTER`; the GLTF
  module = glTF 1 **and** glTF 2 together. Narrowing verified via 66
  `ASSIMP_BUILD_NO_*` defines in `assimp-build/.../flags.make`.
- `DefaultIOSystem` is `final` but public — compose/delegate, don't subclass.
- IOSystem-bypass audit (v6.0.5): clean — see §4.5.

**stb_image (v2.30, vendored in `extern/stb/`):**
- `stbi_info_from_memory` = header-only (w/h/channels), never decodes — the
  safe default for metadata. Full decode allocates `w*h*4`.
- `STBI_MAX_DIMENSIONS` defaults to `1<<24`; internal decoded-size guards
  exist — treat as backstop only, cap sizes earlier.
- 16-bit PNG/PSD and HDR are silently reduced to 8-bit through the pipeline.

**Repo conventions:**
- Flatbuffers: `gen_flatbuffer_cpp` (cmake/) per schema (+ `SCHEMA_TARGETS`
  for includes); runtime JSON against embedded schemas: `embed_fbs_schema` +
  `toolutils::ParseEmbeddedSchema` — see `staged-asset-manifest.cc` for the
  working pattern (incl. its global LoadFileFunction juggling).
- **No C++ unit test framework** — plain-assert selftest binaries
  (`staged-asset-io-selftest` is the exemplar). e2e = pytest; conftest
  pattern: `--<tool>-bin` pytest options with env fallbacks
  (`IGASSET_GEN_BIN`, `ENUMERATE_IGASSET_BIN`, `IGASSET_TEST_ASSET_ROOT`).
- C++23, CLI11, spdlog (`log->clone("Name")` per component),
  `.clang-format` at repo root — run `clang-format -i` on touched files.

**staged-asset-io gotchas (from selftest development):**
- A single `..` from `"models/"` resolves to a top-level logical name —
  still inside the namespace; only popping past the root is rejected.
  Correct by design; don't "fix" it.
- Manifest keys are stored **normalized**; `physical_for` does NOT
  normalize — callers must call `normalize_logical_reference` first.
- Physical names are bare filenames only (UUID files sit flat in the
  staging root).

**Forgejo + CI + environment (2026-10-12 audit — previously unrecorded):**
- Remote: Forgejo, `ssh://git@git.indigocode.dev/indigocode/igasset.git`.
  The dev environment has Forgejo API access as `igai-agent-bot`
  (collaborator with push rights; NOT admin — cannot read branch
  protections).
- Repo merge policy: **squash-merge only** (merge commits + rebase
  disabled in repo settings); the entire history is squash-merged PRs.
  Landing convention: topic branch → PR → CI → squash-merge.
- **CI exists** (`.forgejo/workflows/`, two workflows; both trigger on push
  to `main` AND `pull_request_target`):
  1. `igasset-gen-integration.yaml` — configures **clang/clang++ with
     Ninja** (`CMAKE_CXX_SCAN_FOR_MODULES=OFF`), builds igasset-gen /
     enumerate-igasset / igpack-bundle / enumerate-igpack /
     **extract-asset-metadata / staged-asset-io-selftest**, runs the
     selftest + ALL THREE pytest e2e suites (env vars), including
     `tools/extract-asset-metadata/e2e/` (added milestone 4).
  2. `build-wasm.yaml` — emscripten-release preset; builds only the
     `igasset` WASM target. Unaffected by Phase 0+1+2 code (gen tools +
     assimp/stb/picosha2/CLI11 are gated behind
     `NOT EMSCRIPTEN AND IGASSET_BUILD_GEN_TOOLS`).
- **Compiler-parity gap: CLOSED (2026-10-12, session 2).** A local
  clang 21.1.8 + Ninja parity build (`build-clang/`, CI-matching flags:
  `-G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
  -DCMAKE_CXX_SCAN_FOR_MODULES=OFF -DIGASYNC_BUILD_TESTS=OFF
  -DIGASYNC_BUILD_EXAMPLES=OFF -DFETCHCONTENT_UPDATES_DISCONNECTED=ON`)
  compiled **everything — all deps rebuilt from scratch with clang —
  with zero errors and zero parity fixes needed**. Validated against the
  clang binaries: selftest 77/77, both e2e suites 23/23, SHA-256 output
  identical to GCC + sha256sum, exit codes correct. Config parity
  confirmed (Release + `IGASSET_ENABLE_BASISU_SUPPORT=ON` both trees).
  `build*/` is now gitignored. Re-run this parity build before pushing if
  further C++ changes land (cheap now: `cmake --build build-clang`).
- The `extern/CMakeLists.txt` assimp change invalidates the CI
  dependency-cache key (`hashFiles('extern/CMakeLists.txt')`) → the first
  CI run after push re-fetches + rebuilds all deps (~several minutes;
  expected, not a failure).
- **CWD pitfall:** the VS Code multi-root workspace
  (`igasset-viewer-react.code-workspace`) opens viewer-react + igasync +
  igasset together; fresh sessions often start CWD'd in
  `igasset-viewer-react` (a DIFFERENT repo). Always operate on
  `/home/kamaron/dev/igasset` explicitly (`git -C`, absolute paths).
- A branch named `extract-metadata-binary` was accidentally created in the
  igasset-viewer-react repo on 2026-10-11 (zero commits, unused) — safe to
  delete there; the real work branch is in igasset.

## 9. Current tree state (updated 2026-10-12, session 2)

**Branch `extract-metadata-binary`** (from `main` @ `3d98eb7` =
origin/main; not pushed). Commit 1 = Phase 0+1 (19 files, +707/−972).
Commit 2 (session 2, milestone 1) = Phase-2 scaffold: `with_extra_entry`
(impl + 10 selftest checks → **77 total, ALL PASS**),
`tools/extract-asset-metadata/` (CMakeLists + 4 headers + 7 sources:
final CLI/main/renderers, stubbed detectors), `tools/CMakeLists.txt`
registration, doc updates. Working tree clean after each milestone
commit.

Build/test commands (repo root; build dir = `build/`):

```sh
cmake -S . -B build            # reconfigure (required after CMake changes)
cmake --build build -j8        # or per-target:
cmake --build build --target staged-asset-io-selftest igasset-gen \
    igpack-bundle enumerate-igasset enumerate-igpack enumerate-assimp -j8

./build/tools/staged-asset-io/staged-asset-io-selftest

python3 -m pytest tools/igasset-gen/e2e/ \
    --igasset-gen-bin=build/tools/igasset-gen/igasset-gen \
    --enumerate-igasset-bin=build/tools/enumerate-igasset/enumerate-igasset

python3 -m pytest tools/igpack-bundle/e2e/ \
    --igasset-gen-bin=build/tools/igasset-gen/igasset-gen \
    --igpack-bundle-bin=build/tools/igpack-bundle/igpack-bundle \
    --enumerate-igpack-bin=build/tools/enumerate-igpack/enumerate-igpack
```

Expected baseline (re-confirmed 2026-10-12 against the final binaries):
selftest **ALL PASS (67 checks)**; both suites **23 passed** each. Run
pytest with `-p no:cacheprovider` + `PYTHONDONTWRITEBYTECODE=1` to avoid
dropping artifacts into the tree (the e2e dirs' `.gitignore` covers
`__pycache__` and pytest self-ignores `.pytest_cache`, so either way is
safe for commits — but keep the tree clean).

Known environment quirks observed in the previous sessions (may or may not
reproduce elsewhere): the shell-integration occasionally loses command
completion (commands still run; use exit-code echoes + log files; **this
reproduced 2026-10-12**); the assimp re-fetch after the version bump takes
several minutes (run builds in the background and poll a log file); the
build directory must be reconfigured (`cmake -S . -B build`) before new
targets exist.

## 10. Recommended first steps for a fresh session

1. **Read this document fully**, then skim, in this order:
   `tools/staged-asset-io/include/staged-asset-io/*.h` (the whole API is
   six small headers), `schema/staged-asset-manifest.fbs`,
   `schema/igasset-gen-plan.fbs` (the action types extract-asset-metadata
   reports against), and `tools/igasset-gen/include/igasset-gen/stb-parse.h`
   (image constraints cited in §5.4).
2. Rebuild + run the selftest + both e2e suites (§9) to confirm the
   baseline before touching anything.
3. ~~Commit Phase 0+1~~ **DONE (2026-10-12):** committed on
   `extract-metadata-binary`; keep updating the status table as phases
   complete. Before pushing: run the local clang + Ninja parity build
   (§8), then push the branch + open a PR (squash-merge repo) and let CI
   validate.
4. Start **Phase 2 (§5)** (§5.11 defaults are resolved — see there):
   scaffold `tools/extract-asset-metadata/` from the `enumerate-igpack`
   CMake pattern; implement in this order, validating each step against
   real files: detection (image → flatbuffers → text → assimp sniff) →
   text report → `--json` → dependencies → `--deep` → fixtures + e2e →
   CI workflow update (§5.9). Add `test_assets/` model fixtures when you
   reach the dependency tests (§5.10).
5. Then **Phase 3 (§6)**, keeping the direct-mode invariant front and
   center: existing e2e suites must stay green with zero fixture changes.

*— End of handoff document —*