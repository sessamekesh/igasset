#ifndef EXTRACT_ASSET_METADATA_DETECT_H
#define EXTRACT_ASSET_METADATA_DETECT_H

#include <extract-asset-metadata/metadata.h>
#include <spdlog/logger.h>
#include <staged-asset-io/staged-asset-manifest.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace assetmeta {

// A raw dependency reference as written inside the inspected file, before
// normalization / truncation / status resolution.
struct RawDependency {
  std::string reference;
  std::string source;     // e.g. "obj:mtllib"
  bool embedded = false;  // data embedded in the file itself (data: URI)

  // Set for references observed during a --deep import (the Assimp access
  // journal). journal_allowed distinguishes a successful (manifest-hit)
  // open from a denied (unmapped) access; per plan section 5.5 these
  // resolve to Resolved / Unmapped in staged mode instead of the plain
  // manifest-lookup statuses.
  bool from_journal = false;
  bool journal_allowed = false;
};

// Model text-scan / binary-magic results (OBJ, MTL, glTF, GLB, FBX).
struct ModelScan {
  Kind kind = Kind::Unknown;
  std::string format;  // "obj" / "mtl" / "gltf" / "glb" / "fbx"
  std::vector<RawDependency> dependencies;
  bool light_scan_limited = false;
  // Importer-reported format name when this scan came from the Assimp
  // content sniff (empty for the magic-byte and text-scan paths).
  std::string importer_name;
};

// Image detection (magic bytes + header-only stb_image inspection, D5/D8).
// Sets *kind on a hit. Returns nullopt when the content is not one of the
// supported image formats. has_dims is false when the header could not be
// parsed (e.g. an ASCII PNM variant, or a corrupt header).
std::optional<ImageInfo> detect_image(const std::string& bytes, Kind* kind);

// Model detection: binary magics (binary FBX, GLB) first, then text
// heuristics (OBJ, MTL, glTF JSON, ASCII FBX). Returns nullopt when the
// content does not look like any supported model format.
std::optional<ModelScan> detect_model(const std::string& bytes);

// Last-resort Assimp content sniff over the loaded bytes (only the three
// compiled importers exist in this build). Returns a ModelScan carrying the
// sniffed kind, the importer-reported format name and no dependencies
// (the sniff cannot extract them) when a compiled importer claims the
// content, or nullopt. The kind comes from the selected importer index -
// never from string-matching the importer name.
std::optional<ModelScan> sniff_assimp(const std::string& bytes);

// WGSL entry-point scan (D13: heuristics only, no parser). Returns
// nullopt when no @vertex/@fragment/@compute entry point is found.
std::optional<ShaderInfo> detect_shader(const std::string& bytes);

// The result of a full Assimp import (--deep).
struct DeepImportResult {
  bool ok = false;
  std::string error;  // Assimp's error string when !ok (exit code 3)
  ModelInfo model;    // deep fields populated when ok
  std::vector<RawDependency> dependencies;  // journal opens + material
                                            // texture paths
};

// Full Assimp import of the input (--deep, model kinds only - the entire
// Assimp attack surface, opt-in by the caller).
//
//   Staged mode: pass the loaded manifest + staging root (normally the
//     parent directory of the physical input). A manifest entry for the
//     main input is synthesized when missing (see
//     StagedAssetManifest::with_extra_entry) and the import resolves
//     through AssimpStagedIOSystem by LOGICAL name (with its original
//     extension, so Assimp's extension-based importer selection works).
//   Direct mode: pass a null manifest; the physical path is imported
//     through AssimpRecordingIOSystem.
//
// base_dir is the logical parent directory of the input
// (stagedio::logical_parent_dir(logical_name)) - recorded direct-mode
// paths are normalized against it.
DeepImportResult deep_import_model(
    const std::string& format, const std::filesystem::path& physical_path,
    const std::string& logical_name, const std::string& base_dir,
    const std::shared_ptr<const stagedio::StagedAssetManifest>& manifest,
    const std::filesystem::path& staging_root,
    const std::shared_ptr<spdlog::logger>& log);

}  // namespace assetmeta

#endif