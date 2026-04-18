#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <igasset-gen/assimp-util.h>
#include <spdlog/logger.h>

#include <assimp/Importer.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace igassetgen {

std::shared_ptr<AssimpSceneData> load_scene(
    const std::string& file_extension, const std::string& file_data,
    const std::string& igasset_name, std::shared_ptr<spdlog::logger> log) {
  if (file_data.size() == 0u) {
    log->error("Failed to process empty file into AssimpSceneData");
    return nullptr;
  }

  auto tr = std::make_shared<AssimpSceneData>();
  tr->importer = std::make_shared<Assimp::Importer>();
  tr->importer->SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
  // Import speed isn't super important for the build step compared to final
  // output - we can pay the extra cost to generate tangent/bitangents, remove
  // degenerate polygons, triangulate the mesh, etc.
  const uint32_t import_flags = aiProcessPreset_TargetRealtime_Quality |
                                aiProcess_FlipUVs | aiProcess_CalcTangentSpace |
                                aiProcess_FixInfacingNormals;

  tr->scene = tr->importer->ReadFileFromMemory(
      &file_data[0], file_data.size(), import_flags, file_extension.c_str());

  if (!tr->scene) {
    log->error("Failed to process geometry requested for {}: {}", igasset_name,
               tr->importer->GetErrorString());
    return nullptr;
  }

  return tr;
}

}  // namespace igassetgen
