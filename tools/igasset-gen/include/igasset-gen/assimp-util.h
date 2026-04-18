#ifndef IGASSET_GEN_ASSIMP_UTIL_H
#define IGASSET_GEN_ASSIMP_UTIL_H

#include <assimp/scene.h>
#include <spdlog/spdlog.h>

#include <assimp/Importer.hpp>
#include <memory>
#include <string>

namespace igassetgen {

struct AssimpSceneData {
  std::shared_ptr<Assimp::Importer> importer;
  const aiScene* scene;

  AssimpSceneData() : importer(nullptr), scene(nullptr) {}
};

std::shared_ptr<AssimpSceneData> load_scene(
    const std::string& file_extension, const std::string& file_data,
    const std::string& igasset_name, std::shared_ptr<spdlog::logger> log);

}  // namespace igassetgen

#endif
