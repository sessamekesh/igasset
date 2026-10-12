#include <extract-asset-metadata/detect.h>

#include <staged-asset-io/assimp-import.h>
#include <staged-asset-io/assimp-io-system.h>
#include <staged-asset-io/file-access-journal.h>
#include <staged-asset-io/reference-normalize.h>
#include <staged-asset-io/staged-asset-manifest.h>

#include <assimp/BaseImporter.h>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <spdlog/logger.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace assetmeta {
namespace {

// The physical directory prefix that the glTF importers prepend to
// dependency requests (they derive it from the path passed to ReadFile).
// Stripped from recorded direct-mode requests so the remainder can be
// interpreted as a reference relative to the input.
std::string_view strip_physical_prefix(
    std::string_view request, const std::filesystem::path& physical_path) {
  const std::string parent = physical_path.parent_path().string();
  if (parent.empty() || request.size() <= parent.size() + 1) {
    return request;
  }
  if (request.compare(0, parent.size(), parent) == 0 &&
      (request[parent.size()] == '/' || request[parent.size()] == '\\')) {
    return request.substr(parent.size() + 1);
  }
  return request;
}

void collect_mesh_inventory(const aiScene* scene, ModelInfo* info) {
  for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
    const aiMesh* mesh = scene->mMeshes[m];
    MeshInfo mesh_info;
    mesh_info.name = mesh->mName.C_Str();
    mesh_info.vertices = mesh->mNumVertices;
    mesh_info.faces = mesh->mNumFaces;
    mesh_info.has_texcoords = mesh->mTextureCoords[0] != nullptr;
    mesh_info.has_bones = mesh->mNumBones > 0;
    info->meshes.push_back(std::move(mesh_info));
  }

  // Selection groups: which meshes may be selected together in one
  // AssimpToDracoAction (include_texcoords / include_bones must be uniform
  // across the selection). Fixed combination order keeps the report
  // deterministic.
  struct Combo {
    bool texcoords;
    bool bones;
  };
  static const Combo kCombos[] = {
      {true, true}, {true, false}, {false, true}, {false, false}};
  for (const auto& combo : kCombos) {
    MeshSelectionGroup group;
    group.include_texcoords = combo.texcoords;
    group.include_bones = combo.bones;
    for (const auto& mesh : info->meshes) {
      if (mesh.has_texcoords == combo.texcoords &&
          mesh.has_bones == combo.bones) {
        group.mesh_names.push_back(mesh.name);
      }
    }
    if (!group.mesh_names.empty()) {
      info->selection_groups.push_back(std::move(group));
    }
  }
}

// Materials plus their texture paths. External texture paths become
// dependencies; '*' -prefixed paths are Assimp's embedded-texture
// references (content inside the model file) and are reported as embedded.
void collect_materials(const aiScene* scene, ModelInfo* info,
                       std::vector<RawDependency>* deps,
                       std::vector<std::string>* texture_refs_seen) {
  for (unsigned int m = 0; m < scene->mNumMaterials; ++m) {
    const aiMaterial* material = scene->mMaterials[m];
    MaterialInfo mat_info;
    aiString material_name;
    if (material->Get(AI_MATKEY_NAME, material_name) == aiReturn_SUCCESS) {
      mat_info.name = material_name.C_Str();
    }

    for (int type = aiTextureType_DIFFUSE; type <= aiTextureType_UNKNOWN;
         ++type) {
      const unsigned int count =
          material->GetTextureCount(static_cast<aiTextureType>(type));
      for (unsigned int t = 0; t < count; ++t) {
        aiString path;
        if (material->GetTexture(static_cast<aiTextureType>(type), t, &path) !=
            aiReturn_SUCCESS) {
          continue;
        }
        const std::string texture_path = path.C_Str();
        if (texture_path.empty()) {
          continue;
        }
        mat_info.texture_paths.push_back(texture_path);

        if (texture_path[0] == '*') {
          // Embedded texture (index into scene->mTextures).
          RawDependency dep;
          dep.reference = texture_path;
          dep.source = "assimp:material-texture";
          dep.embedded = true;
          deps->push_back(std::move(dep));
          continue;
        }

        bool seen = false;
        for (const auto& ref : *texture_refs_seen) {
          if (ref == texture_path) {
            seen = true;
            break;
          }
        }
        if (!seen) {
          texture_refs_seen->push_back(texture_path);
          RawDependency dep;
          dep.reference = texture_path;
          dep.source = "assimp:material-texture";
          deps->push_back(std::move(dep));
        }
      }
    }
    info->materials.push_back(std::move(mat_info));
  }
}

// Journal opens (minus the main file) become dependencies. Staged records
// are already-normalized logical names; direct records are raw Assimp
// requests (physical-prefix-stripped, then normalized against base_dir).
void collect_journal_dependencies(const stagedio::FileAccessJournal& journal,
                                  bool staged_mode,
                                  const std::string& main_request_path,
                                  const std::string& base_dir,
                                  const std::filesystem::path& physical_path,
                                  std::vector<RawDependency>* deps) {
  for (const auto& record : journal.snapshot()) {
    if (record.logical_path == main_request_path) {
      continue;  // the main file itself is not a dependency
    }

    RawDependency dep;
    dep.from_journal = true;
    dep.journal_allowed = record.allowed;
    dep.source = "assimp:journal";

    if (staged_mode) {
      dep.reference = record.logical_path;
    } else {
      const std::string_view stripped =
          strip_physical_prefix(record.logical_path, physical_path);
      auto normalized =
          stagedio::normalize_logical_reference(base_dir, stripped);
      dep.reference =
          normalized.has_value() ? *normalized : std::string(stripped);
    }
    deps->push_back(std::move(dep));
  }
}

}  // namespace

DeepImportResult deep_import_model(
    const std::string& format, const std::filesystem::path& physical_path,
    const std::string& logical_name, const std::string& base_dir,
    const std::shared_ptr<const stagedio::StagedAssetManifest>& manifest,
    const std::filesystem::path& staging_root,
    const std::shared_ptr<spdlog::logger>& log) {
  DeepImportResult result;
  const bool staged_mode = manifest != nullptr;

  std::shared_ptr<const stagedio::StagedAssetManifest> effective_manifest =
      manifest;
  auto journal = std::make_shared<stagedio::FileAccessJournal>();
  std::filesystem::path read_path;
  std::string main_request_path;

  Assimp::IOSystem* io = nullptr;
  if (staged_mode) {
    // Synthesize a runtime mapping for the input when the orchestrator's
    // manifest does not contain it (main.cc has already verified that an
    // existing mapping points at this exact file).
    effective_manifest = manifest->with_extra_entry(
        logical_name, physical_path.filename().string(), log);
    if (effective_manifest == nullptr) {
      result.error =
          "failed to synthesize a staged manifest entry for the input";
      return result;
    }
    io = new stagedio::AssimpStagedIOSystem(staging_root, effective_manifest,
                                            journal, log, base_dir);
    // Assimp selects importers by the EXTENSION of the path it is given:
    // in staged mode that must be the logical name (with its original
    // extension); the IOSystem maps it to the UUID-named physical file.
    read_path = std::filesystem::path(logical_name);
    // The journal records the RESOLVED (normalized) name for the main
    // file; the skip comparison in collect_journal_dependencies must use
    // that same form.
    auto normalized_main =
        stagedio::normalize_logical_reference("", logical_name);
    main_request_path =
        normalized_main.has_value() ? *normalized_main : logical_name;
  } else {
    io = new stagedio::AssimpRecordingIOSystem(journal);
    read_path = physical_path;
    main_request_path = physical_path.string();
  }
  // load_scene's Importer takes ownership of (and deletes) io.

  std::string error;
  auto holder = stagedio::load_scene(read_path, logical_name, log, io, &error);
  if (holder == nullptr || holder->scene == nullptr) {
    result.error = error.empty() ? "Assimp import failed" : error;
    return result;
  }

  const aiScene* scene = holder->scene;
  ModelInfo info;
  info.format = format;
  info.deep_scanned = true;

  // Importer display name via the (already-selected) importer. NOTE:
  // Importer::GetImporterIndex takes the BARE extension ("fbx"), not a
  // path - it compares its argument verbatim against each importer's
  // extension list.
  std::string read_extension = read_path.extension().string();
  if (!read_extension.empty() && read_extension[0] == '.') {
    read_extension.erase(0, 1);
  }
  const std::size_t importer_index =
      holder->importer->GetImporterIndex(read_extension.c_str());
  if (!read_extension.empty() &&
      importer_index != static_cast<std::size_t>(-1)) {
    const auto* imp = holder->importer->GetImporter(importer_index);
    if (imp != nullptr && imp->GetInfo() != nullptr &&
        imp->GetInfo()->mName != nullptr) {
      info.importer_name = imp->GetInfo()->mName;
    }
  }

  collect_mesh_inventory(scene, &info);
  info.animation_count = scene->mNumAnimations;

  std::vector<RawDependency> deps;
  std::vector<std::string> texture_refs_seen;
  collect_materials(scene, &info, &deps, &texture_refs_seen);
  collect_journal_dependencies(*journal, staged_mode, main_request_path,
                               base_dir, physical_path, &deps);

  result.ok = true;
  result.model = std::move(info);
  result.dependencies = std::move(deps);
  return result;
}

}  // namespace assetmeta
