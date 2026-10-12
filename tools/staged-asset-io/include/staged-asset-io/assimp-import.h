#ifndef STAGED_ASSET_IO_ASSIMP_IMPORT_H
#define STAGED_ASSET_IO_ASSIMP_IMPORT_H

#include <assimp/scene.h>
#include <spdlog/logger.h>
#include <assimp/Importer.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace stagedio {

// The canonical Assimp import flag set used for IgAsset generation. Shared
// (rather than duplicated per tool) so that metadata extraction
// (extract-asset-metadata --deep) reports exactly what igasset-gen's
// AssimpToDracoAction will see.
std::uint32_t igasset_import_flags();

struct AssimpSceneData {
  std::shared_ptr<Assimp::Importer> importer;
  const aiScene* scene;

  AssimpSceneData() : importer(nullptr), scene(nullptr) {}
};

// Import a scene from a model file.
//
// @param input_file_path
// - Direct policy: path of the model file on disk.
// - Staged policy: the LOGICAL name of the model (with its original
//   extension, so Assimp's extension-based importer selection works). All
//   file accesses - this one and any referenced files - then resolve through
//   the custom IOSystem.
//
// @param custom_io
// Optional Assimp IOSystem adapter (see assimp-io-system.h). The Importer
// takes ownership of it and deletes it when done; anything that must
// outlive the import (e.g. the access journal) must be shared state, not
// IOSystem state.
//
// @param error_out
// Optional; when non-null and the import fails, receives Assimp's error
// string (the same text that is logged).
//
// Returns nullptr on failure (error logged).
std::shared_ptr<AssimpSceneData> load_scene(
    const std::filesystem::path& input_file_path,
    const std::string& igasset_name, const std::shared_ptr<spdlog::logger>& log,
    Assimp::IOSystem* custom_io = nullptr, std::string* error_out = nullptr);

}  // namespace stagedio

#endif