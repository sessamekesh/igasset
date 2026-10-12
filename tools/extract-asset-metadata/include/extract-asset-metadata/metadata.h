#ifndef EXTRACT_ASSET_METADATA_METADATA_H
#define EXTRACT_ASSET_METADATA_METADATA_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace assetmeta {

// The detected kind of the inspected file - by CONTENT, not extension (D8).
enum class Kind {
  Png,
  Jpeg,
  Tga,
  Bmp,
  Psd,
  Gif,
  Hdr,
  Pic,
  Pnm,
  Fbx,
  Gltf,
  Glb,
  Obj,
  Mtl,
  Wgsl,
  Igasset,
  Igpack,
  Unknown,
};

enum class Category { Image, Model, Shader, Generated, Unknown };

// How a dependency's availability was resolved. D11: staged mode NEVER
// probes the filesystem - status comes from pure manifest lookup (refined
// by the Assimp access journal in --deep mode); direct mode may probe.
enum class DepStatus {
  InManifest,     // staged: mapped by the manifest (lookup only)
  NotInManifest,  // staged: not mapped by the manifest (lookup only)
  Resolved,       // --deep journal: the file was opened; direct: exists
  Unmapped,       // --deep journal (staged): access was denied
  Missing,        // direct: does not exist on disk
  Embedded,       // data embedded in the file itself (e.g. a data: URI)
};

const char* to_string(Kind kind);
const char* to_string(Category category);
const char* to_string(DepStatus status);

// --- per-kind payloads -----------------------------------------------------

struct PngDetails {
  int bit_depth = 0;
  int color_type = 0;
  bool interlaced = false;
  bool has_srgb_chunk = false;
  bool has_gama_chunk = false;
};

struct JpegDetails {
  bool progressive = false;
};

struct GifDetails {
  // Number of image descriptors found by a bounded block walk; 0 when the
  // walk did not complete cleanly.
  std::uint64_t frame_count = 0;
  bool frame_count_complete = false;
};

struct PnmDetails {
  // The PNM signature variant (1..6). stb_image only consumes binary PNM
  // (P5/P6) - other variants are reported with a warning and no actions.
  int variant = 0;
};

struct ImageInfo {
  int width = 0;
  int height = 0;
  int channels = 0;
  bool has_dims = false;  // false when the header could not be parsed

  std::optional<PngDetails> png;
  std::optional<JpegDetails> jpeg;
  std::optional<GifDetails> gif;
  std::optional<PnmDetails> pnm;
};

struct MeshInfo {
  std::string name;
  std::uint64_t vertices = 0;
  std::uint64_t faces = 0;
  bool has_texcoords = false;
  bool has_bones = false;
};

// A group of meshes that may be selected together in one
// AssimpToDracoAction (include_texcoords / include_bones must be uniform
// across the selected meshes - the igasset-gen processor validates this).
struct MeshSelectionGroup {
  bool include_texcoords = false;
  bool include_bones = false;
  std::vector<std::string> mesh_names;
};

struct MaterialInfo {
  std::string name;
  std::vector<std::string> texture_paths;  // raw references, pre-truncation
};

struct ModelInfo {
  std::string format;  // "obj" / "mtl" / "gltf" / "glb" / "fbx"

  // Light-scan limitation (e.g. binary FBX without --deep cannot report
  // mesh names or dependencies).
  bool light_scan_limited = false;

  // --- --deep only ---
  std::string importer_name;
  std::vector<MeshInfo> meshes;
  std::vector<MeshSelectionGroup> selection_groups;
  std::uint64_t animation_count = 0;
  std::vector<MaterialInfo> materials;
  bool deep_scanned = false;
};

struct WgslEntryPoint {
  std::string stage;  // "vertex" / "fragment" / "compute"
  std::string name;
};

struct ShaderInfo {
  std::vector<WgslEntryPoint> entry_points;
};

struct GeneratedInfo {
  std::string inspect_with;  // suggested existing inspection tool
};

// --- advice + dependencies ---------------------------------------------------

// An igasset-gen plan action that can consume the inspected file, plus
// parameter guidance / caveats.
struct ActionAdvice {
  std::string action;
  std::vector<std::string> notes;
};

struct Dependency {
  std::string name;    // logical name, truncated to 256 code points (D7)
  std::string source;  // e.g. "obj:mtllib", "gltf:uri", "assimp:journal"
  DepStatus status = DepStatus::Missing;
  bool truncated = false;
};

// The full result of one inspection - rendered by report-text/report-json.
struct AssetMetadata {
  Kind kind = Kind::Unknown;
  Category category = Category::Unknown;
  std::filesystem::path physical_path;
  std::string logical_name;
  bool staged_mode = false;
  std::uint64_t file_size = 0;
  std::string sha256;

  std::optional<ImageInfo> image;
  std::optional<ModelInfo> model;
  std::optional<ShaderInfo> shader;
  std::optional<GeneratedInfo> generated;

  std::vector<ActionAdvice> actions;
  std::vector<Dependency> dependencies;

  // Informational (non-problematic) messages for the caller.
  std::vector<std::string> notes;

  // Things that will produce wrong output or failed generation if the
  // caller proceeds with the suggested actions.
  std::vector<std::string> warnings;
};

}  // namespace assetmeta

#endif