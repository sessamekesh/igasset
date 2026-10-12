#include <picosha2.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <staged-asset-io/reference-normalize.h>
#include <staged-asset-io/staged-asset-manifest.h>
#include <CLI/CLI.hpp>

#include <extract-asset-metadata/detect.h>
#include <extract-asset-metadata/metadata.h>
#include <extract-asset-metadata/report.h>
#include <extract-asset-metadata/schema/igasset.h>
#include <extract-asset-metadata/schema/igpack.h>
#include <extract-asset-metadata/utf8.h>

#include <flatbuffers/verifier.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uintmax_t kDefaultMaxFileSize = 1024ull << 30;  // 1 GiB

constexpr int kExitOk = 0;
constexpr int kExitBadInput = 1;
constexpr int kExitTooLarge = 2;
constexpr int kExitDeepParseFailure = 3;

constexpr std::size_t kReadChunkSize = 64 * 1024;

enum class ReadResult { Ok, OpenFailed, TooLarge };

// Reads the whole input file while feeding the SHA-256 hasher chunk-by-chunk
// (never a second pass over the bytes). Defends against the file growing
// between the pre-read size check and the read itself (untrusted input).
ReadResult read_input_file(const std::filesystem::path& path,
                           std::uintmax_t max_file_size,
                           std::uintmax_t expected_size, std::string& bytes_out,
                           std::string& sha256_out) {
  std::ifstream fin(path, std::ios::binary);
  if (!fin) {
    return ReadResult::OpenFailed;
  }

  std::string bytes;
  bytes.reserve(static_cast<std::size_t>(
      std::min<std::uintmax_t>(expected_size, 256ull << 20)));

  picosha2::hash256_one_by_one hasher;
  std::vector<char> chunk(kReadChunkSize);
  while (fin.read(chunk.data(), kReadChunkSize) || fin.gcount() > 0) {
    const std::streamsize got = fin.gcount();
    if (got > 0) {
      hasher.process(chunk.data(), chunk.data() + got);
      bytes.append(chunk.data(), static_cast<std::size_t>(got));
    }
    if (bytes.size() > max_file_size) {
      return ReadResult::TooLarge;
    }
  }
  if (fin.bad()) {
    return ReadResult::OpenFailed;
  }

  hasher.finish();
  sha256_out = picosha2::get_hash_hex_string(hasher);
  bytes_out = std::move(bytes);
  return ReadResult::Ok;
}

bool is_image_kind(assetmeta::Kind kind) {
  switch (kind) {
    case assetmeta::Kind::Png:
    case assetmeta::Kind::Jpeg:
    case assetmeta::Kind::Tga:
    case assetmeta::Kind::Bmp:
    case assetmeta::Kind::Psd:
    case assetmeta::Kind::Gif:
    case assetmeta::Kind::Hdr:
    case assetmeta::Kind::Pic:
    case assetmeta::Kind::Pnm:
      return true;
    default:
      return false;
  }
}

// Builds the igasset-gen action advice + pipeline caveats for the detected
// kind (plan section 5.4). Driven purely by the metadata structs, so it is
// identical in text and JSON modes.
void apply_action_advice(assetmeta::AssetMetadata& meta) {
  using assetmeta::ActionAdvice;
  using assetmeta::Kind;

  if (is_image_kind(meta.kind)) {
    // PNM variants the pipeline cannot consume are reported with a
    // warning and NO actions (stb_image only consumes binary PNM).
    if (meta.kind == Kind::Pnm && meta.image.has_value() &&
        meta.image->pnm.has_value() && meta.image->pnm->variant != 5 &&
        meta.image->pnm->variant != 6) {
      meta.warnings.push_back(
          "stb_image (and therefore igasset-gen) only consumes binary PNM "
          "(P5/P6); this file is an ASCII variant and cannot be used");
      return;
    }

    ActionAdvice to_texture;
    to_texture.action = "ImageToTexture2DAction";
    to_texture.notes.push_back(
        "consumed via an Output2DImage block (fields: encoding, explicit "
        "width/height, quality, use_srgb, gen_mips)");
    meta.actions.push_back(std::move(to_texture));

    ActionAdvice spritesheet;
    spritesheet.action = "GenerateSpritesheetAction";
    spritesheet.notes.push_back("usable as a sprite input; crop fields apply");
    meta.actions.push_back(std::move(spritesheet));

    if (meta.image.has_value() && !meta.image->has_dims) {
      meta.warnings.push_back(
          "the image header could not be parsed; the igasset-gen pipeline "
          "will likely fail to decode this file");
      return;
    }
    if (meta.image.has_value()) {
      const auto& image = *meta.image;
      const bool over_8bit = (meta.kind == Kind::Png && image.png.has_value() &&
                              image.png->bit_depth > 8) ||
                             meta.kind == Kind::Psd || meta.kind == Kind::Hdr;
      if (over_8bit) {
        meta.warnings.push_back(
            "source precision above 8 bits is silently reduced to 8 bits by "
            "the igasset-gen image pipeline");
      }
      if (image.has_dims && (image.channels == 1 || image.channels == 2)) {
        meta.warnings.push_back(
            "igasset-gen image resize only supports 3/4-channel images");
        meta.warnings.push_back(
            "1/2-channel inputs currently produce out-of-bounds pixels in "
            "igasset-gen RGBA8 output (known bug, fix pending); generation "
            "may fail or embed garbage channel data");
      }
    }
    return;
  }

  if (meta.kind == Kind::Obj || meta.kind == Kind::Gltf ||
      meta.kind == Kind::Glb || meta.kind == Kind::Fbx) {
    ActionAdvice to_draco;
    to_draco.action = "AssimpToDracoAction";
    to_draco.notes.push_back(
        "meshes are selected by name via assimp_mesh_names");
    to_draco.notes.push_back(
        "include_texcoords / include_bones must be uniform across all "
        "selected meshes (the processor validates this)");
    if (meta.model.has_value() && !meta.model->deep_scanned) {
      to_draco.notes.push_back(
          "run extract-asset-metadata with --deep for the mesh inventory and "
          "valid mesh selection groups");
    }
    meta.actions.push_back(std::move(to_draco));

    if (meta.model.has_value() && meta.model->light_scan_limited) {
      meta.notes.push_back(
          "the light scan of this format is limited; --deep is authoritative "
          "for mesh and dependency information");
    }
    return;
  }

  if (meta.kind == Kind::Mtl) {
    meta.notes.push_back(
        "MTL files are consumed indirectly: reference them from the owning "
        "OBJ's mtllib and stage them together");
    return;
  }

  if (meta.kind == Kind::Wgsl) {
    ActionAdvice copy_wgsl;
    copy_wgsl.action = "CopyWgslSourceAction";
    copy_wgsl.notes.push_back(
        "WGSL sources are copied verbatim into the output .igasset");
    copy_wgsl.notes.push_back(
        "detected entry points map to the action's vertex_entry_point / "
        "fragment_entry_point / compute_entry_point fields");
    meta.actions.push_back(std::move(copy_wgsl));

    if (meta.file_size == 0) {
      meta.warnings.push_back("shader file is empty");
    }
    return;
  }

  if (meta.kind == Kind::Igasset) {
    if (meta.generated.has_value()) {
      meta.generated->inspect_with = "enumerate-igasset";
    }
    meta.notes.push_back(
        "generated IgAsset files are not igasset-gen inputs; inspect with "
        "enumerate-igasset and bundle with igpack-bundle");
    return;
  }

  if (meta.kind == Kind::Igpack) {
    if (meta.generated.has_value()) {
      meta.generated->inspect_with = "enumerate-igpack";
    }
    meta.notes.push_back(
        "generated IgPack bundles are not igasset-gen inputs; inspect with "
        "enumerate-igpack and create with igpack-bundle");
    return;
  }
}

// Resolves scanned dependency references into the reported dependency list:
// normalize against the input's logical parent dir, truncate to 256 code
// points (D7) and resolve status (D11: staged mode NEVER probes the
// filesystem - manifest lookup only; direct mode may probe on disk).
// References to the same file from multiple sources (light-scan text +
// deep journal + material textures) are merged: sources are joined and
// the --deep journal status takes precedence (it reflects an actual
// Assimp open).
std::vector<assetmeta::Dependency> finalize_dependencies(
    const std::vector<assetmeta::RawDependency>& raws,
    std::string_view base_dir, bool staged_mode,
    const std::shared_ptr<const stagedio::StagedAssetManifest>& manifest,
    const std::filesystem::path& direct_probe_root) {
  std::vector<assetmeta::Dependency> deps;
  std::map<std::string, std::size_t> index_by_key;

  // Direct-mode existence probe (trusted input). Uses the error_code
  // overload: untrusted references can produce paths the OS rejects
  // outright (e.g. longer than NAME_MAX), and the throwing overload would
  // terminate the process. Any probe error counts as "does not exist".
  const auto probe_exists = [](const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec) && !ec;
  };

  const auto append_source = [](std::string& existing,
                                const std::string& extra) {
    if (existing.find(extra) == std::string::npos) {
      if (!existing.empty()) {
        existing += ", ";
      }
      existing += extra;
    }
  };

  for (const auto& raw : raws) {
    assetmeta::Dependency dep;
    dep.source = raw.source;

    std::string key;
    if (raw.from_journal) {
      // Journal records are canonical logical names by construction
      // (staged records are the manifest keys that resolved; direct records
      // were already prefix-stripped and normalized in deep_import) - they
      // must NOT be normalized again.
      dep.name = assetmeta::utf8::truncate(raw.reference, 256, &dep.truncated);
      if (staged_mode) {
        dep.status = raw.journal_allowed ? assetmeta::DepStatus::Resolved
                                         : assetmeta::DepStatus::Unmapped;
      } else {
        dep.status = probe_exists(direct_probe_root / raw.reference)
                         ? assetmeta::DepStatus::Resolved
                         : assetmeta::DepStatus::Missing;
      }
      key = "n:" + raw.reference;
    } else if (raw.embedded) {
      dep.name = assetmeta::utf8::truncate(raw.reference, 256, &dep.truncated);
      dep.status = assetmeta::DepStatus::Embedded;
      key = "e:" + raw.reference;
    } else if (raw.reference.find("://") != std::string::npos &&
               raw.reference.rfind("file:", 0) != 0) {
      // A remote URI (http://, https://, ...) can never resolve inside the
      // logical namespace; report it under the mode's unavailable status.
      dep.name = assetmeta::utf8::truncate(raw.reference, 256, &dep.truncated);
      dep.status = staged_mode ? assetmeta::DepStatus::NotInManifest
                               : assetmeta::DepStatus::Missing;
      key = "u:" + raw.reference;
    } else {
      auto normalized =
          stagedio::normalize_logical_reference(base_dir, raw.reference);
      if (normalized.has_value()) {
        dep.name = assetmeta::utf8::truncate(*normalized, 256, &dep.truncated);
        if (staged_mode) {
          const bool in_manifest =
              manifest != nullptr &&
              manifest->physical_for(*normalized) != nullptr;
          dep.status = in_manifest ? assetmeta::DepStatus::InManifest
                                   : assetmeta::DepStatus::NotInManifest;
        } else {
          dep.status = probe_exists(direct_probe_root / *normalized)
                           ? assetmeta::DepStatus::Resolved
                           : assetmeta::DepStatus::Missing;
        }
        key = "n:" + *normalized;
      } else {
        // A reference that cannot exist in the logical namespace can never
        // resolve; report it with the mode's "unavailable" status.
        dep.name =
            assetmeta::utf8::truncate(raw.reference, 256, &dep.truncated);
        dep.status = staged_mode ? assetmeta::DepStatus::NotInManifest
                                 : assetmeta::DepStatus::Missing;
        key = "u:" + raw.reference;
      }
    }

    const auto it = index_by_key.find(key);
    if (it != index_by_key.end()) {
      append_source(deps[it->second].source, raw.source);
      if (raw.from_journal && staged_mode) {
        deps[it->second].status = dep.status;
      }
      continue;
    }
    index_by_key.emplace(key, deps.size());
    deps.push_back(std::move(dep));
  }
  return deps;
}

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{
      "extract-asset-metadata - inspect one file and report what it is, how "
      "igasset-gen can consume it, and which files it depends on\n\n"
      "Staged mode (--manifest): the input is an untrusted upload inside a "
      "per-job staging directory; every logical name resolves through the "
      "manifest and nothing outside the staged set is ever touched. The "
      "physical input file is expected to sit in the staging root (its "
      "manifest physical_path).\n"
      "Direct mode (no --manifest): trusted local files, historical "
      "filesystem semantics.\n"
      "--deep performs a full Assimp import (models only) - this opts in to "
      "the entire Assimp parser attack surface.",
      "extract-asset-metadata"};

  std::filesystem::path input_path;
  app.add_option("physical_input_file", input_path,
                 "The physical file to inspect")
      ->required()
      ->check(CLI::ExistingFile);

  bool json_output = false;
  app.add_flag("--json", json_output,
               "Emit machine-readable JSON instead of text");

  bool deep = false;
  app.add_flag("--deep", deep,
               "Opt in to a full Assimp import (models only; images stay "
               "header-only). This exposes the entire Assimp parser attack "
               "surface - only run it in a sandbox");

  std::filesystem::path manifest_path;
  app.add_option("--manifest", manifest_path,
                 "Staged-asset manifest (JSON). Present = staged policy; "
                 "absent = direct policy")
      ->check(CLI::ExistingFile);

  std::string logical_name_opt;
  app.add_option("--logical-name", logical_name_opt,
                 "Logical name of the input (staged mode: expected to be a "
                 "manifest logical_path). Default: the physical file's own "
                 "file name");

  std::uintmax_t max_file_size = kDefaultMaxFileSize;
  app.add_option("--max-file-size", max_file_size,
                 "Refuse files larger than this many bytes (default: 1 GiB)")
      ->check(CLI::PositiveNumber);

  try {
    app.parse(argc, argv);
  } catch (const CLI::CallForHelp& e) {
    return app.exit(e);
  } catch (const CLI::ParseError& e) {
    app.exit(e);
    return kExitBadInput;
  }

  auto log = spdlog::stderr_color_mt("extract-asset-metadata");
  log->set_level(spdlog::level::err);

  // Size cap BEFORE any read (plan section 5.7).
  std::error_code ec;
  const std::uintmax_t file_size = std::filesystem::file_size(input_path, ec);
  if (ec) {
    log->error("Cannot stat {}: {}", input_path.string(), ec.message());
    return kExitBadInput;
  }
  if (file_size > max_file_size) {
    log->error("File {} is {} bytes, exceeding the {} byte limit",
               input_path.string(), file_size, max_file_size);
    return kExitTooLarge;
  }

  std::string bytes;
  std::string sha256;
  switch (
      read_input_file(input_path, max_file_size, file_size, bytes, sha256)) {
    case ReadResult::OpenFailed:
      log->error("Failed to read {}", input_path.string());
      return kExitBadInput;
    case ReadResult::TooLarge:
      log->error("File {} grew past the {} byte limit while reading",
                 input_path.string(), max_file_size);
      return kExitTooLarge;
    case ReadResult::Ok:
      break;
  }

  std::shared_ptr<const stagedio::StagedAssetManifest> manifest;
  if (!manifest_path.empty()) {
    manifest = stagedio::StagedAssetManifest::load(manifest_path, log);
    if (manifest == nullptr) {
      log->error("Failed to load staged asset manifest {}",
                 manifest_path.string());
      return kExitBadInput;
    }
  }
  const bool staged_mode = manifest != nullptr;

  std::string logical_name = logical_name_opt.empty()
                                 ? input_path.filename().string()
                                 : logical_name_opt;
  if (staged_mode && logical_name_opt.empty()) {
    log->warn(
        "staged mode without --logical-name: defaulting to the physical file "
        "name; dependency status resolution will be inaccurate unless the "
        "manifest maps it");
  }
  const std::string base_dir{stagedio::logical_parent_dir(logical_name)};
  const std::filesystem::path staging_root = input_path.parent_path();

  if (staged_mode &&
      !stagedio::normalize_logical_reference("", logical_name).has_value()) {
    log->error("Invalid logical name: {}", logical_name);
    return kExitBadInput;
  }

  assetmeta::AssetMetadata meta;
  meta.physical_path = input_path;
  meta.logical_name = logical_name;
  meta.staged_mode = staged_mode;
  meta.file_size = file_size;
  meta.sha256 = sha256;

  std::vector<assetmeta::RawDependency> raw_deps;

  // Detection ladder (plan section 5.3): image magic bytes -> flatbuffer
  // probes -> model text/binary scans (before the more generic WGSL
  // heuristics) -> WGSL -> Assimp content sniff -> unknown.
  assetmeta::Kind kind = assetmeta::Kind::Unknown;
  if (auto image = assetmeta::detect_image(bytes, &kind)) {
    meta.kind = kind;
    meta.category = assetmeta::Category::Image;
    meta.image = std::move(*image);
  } else {
    flatbuffers::Verifier verifier(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    if (IgAsset::VerifyAssetBuffer(verifier)) {
      meta.kind = assetmeta::Kind::Igasset;
      meta.category = assetmeta::Category::Generated;
      meta.generated = assetmeta::GeneratedInfo{};
    } else if (IgAsset::VerifyAssetPackBuffer(verifier)) {
      meta.kind = assetmeta::Kind::Igpack;
      meta.category = assetmeta::Category::Generated;
      meta.generated = assetmeta::GeneratedInfo{};
    } else if (auto model = assetmeta::detect_model(bytes)) {
      meta.kind = model->kind;
      meta.category = assetmeta::Category::Model;
      assetmeta::ModelInfo info;
      info.format = model->format;
      info.light_scan_limited = model->light_scan_limited;
      info.importer_name = model->importer_name;
      raw_deps = std::move(model->dependencies);
      meta.model = std::move(info);
    } else if (auto shader = assetmeta::detect_shader(bytes)) {
      meta.kind = assetmeta::Kind::Wgsl;
      meta.category = assetmeta::Category::Shader;
      meta.shader = std::move(*shader);
    } else if (auto sniffed = assetmeta::sniff_assimp(bytes)) {
      meta.kind = sniffed->kind;
      meta.category = assetmeta::Category::Model;
      assetmeta::ModelInfo info;
      info.format = sniffed->format;
      info.light_scan_limited = true;
      info.importer_name = sniffed->importer_name;
      raw_deps = std::move(sniffed->dependencies);
      meta.model = std::move(info);
    } else {
      meta.kind = assetmeta::Kind::Unknown;
      meta.category = assetmeta::Category::Unknown;
    }
  }

  // Deep import (models only; images stay header-only per the resolved
  // open questions, plan section 5.11). MTL files are a dependency kind:
  // Assimp consumes them via the owning OBJ, never standalone.
  if (deep && meta.kind == assetmeta::Kind::Mtl) {
    meta.notes.push_back(
        "--deep is not applicable to MTL files; they are consumed via the "
        "owning OBJ's mtllib");
  } else if (deep && meta.category == assetmeta::Category::Model) {
    if (staged_mode) {
      const auto normalized_input =
          stagedio::normalize_logical_reference("", logical_name);
      const std::string* mapped =
          normalized_input.has_value()
              ? manifest->physical_for(*normalized_input)
              : nullptr;
      if (mapped != nullptr && *mapped != input_path.filename().string()) {
        log->error(
            "Logical name {} is manifest-mapped to physical file {} but the "
            "input is {}",
            logical_name, *mapped, input_path.filename().string());
        return kExitBadInput;
      }
    }

    auto deep_result = assetmeta::deep_import_model(
        meta.model->format, input_path, logical_name, base_dir, manifest,
        staging_root, log);
    if (!deep_result.ok) {
      meta.warnings.push_back("--deep Assimp import failed: " +
                              deep_result.error);
      apply_action_advice(meta);
      meta.dependencies = finalize_dependencies(raw_deps, base_dir, staged_mode,
                                                manifest, staging_root);
      if (json_output) {
        assetmeta::render_json(meta, std::cout);
      } else {
        assetmeta::render_text(meta, std::cout);
      }
      return kExitDeepParseFailure;
    }

    meta.model = std::move(deep_result.model);
    raw_deps.insert(raw_deps.end(), deep_result.dependencies.begin(),
                    deep_result.dependencies.end());
  }

  apply_action_advice(meta);
  meta.dependencies = finalize_dependencies(raw_deps, base_dir, staged_mode,
                                            manifest, staging_root);

  if (json_output) {
    assetmeta::render_json(meta, std::cout);
  } else {
    assetmeta::render_text(meta, std::cout);
  }
  return kExitOk;
}
