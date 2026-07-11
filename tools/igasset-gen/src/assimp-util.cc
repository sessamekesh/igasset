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
    const std::filesystem::path& input_file_path,
    const std::string& igasset_name, std::shared_ptr<spdlog::logger> log) {
  auto tr = std::make_shared<AssimpSceneData>();
  tr->importer = std::make_shared<Assimp::Importer>();
  tr->importer->SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
  // Import speed isn't super important for the build step compared to final
  // output - we can pay the extra cost to generate tangent/bitangents, remove
  // degenerate polygons, triangulate the mesh, etc.
  const uint32_t import_flags = aiProcessPreset_TargetRealtime_Quality |
                                aiProcess_FlipUVs | aiProcess_CalcTangentSpace |
                                aiProcess_FixInfacingNormals;

  // Use ReadFile (not ReadFileFromMemory) so that Assimp can resolve external
  //  file references (e.g. glTF .bin buffers, .mtl files for OBJ, etc.)
  //  relative to the input asset's directory.
  // NOTICE - this may cause IO tasks to be run on an exec thread, which is
  //  not ideal but a necessary evil (ASSIMP does not provide a workable
  //  async file i/o abstraction!)
  tr->scene = tr->importer->ReadFile(input_file_path.string(), import_flags);

  if (!tr->scene) {
    log->error("Failed to process geometry requested for {}: {}", igasset_name,
               tr->importer->GetErrorString());
    return nullptr;
  }

  return tr;
}

}  // namespace igassetgen
