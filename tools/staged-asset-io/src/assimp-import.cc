#include <staged-asset-io/assimp-import.h>

#include <assimp/config.h>
#include <assimp/postprocess.h>

namespace stagedio {

std::uint32_t igasset_import_flags() {
  // Import speed isn't super important for the build step compared to final
  // output - we can pay the extra cost to generate tangent/bitangents,
  // remove degenerate polygons, triangulate the mesh, etc.
  return aiProcessPreset_TargetRealtime_Quality | aiProcess_FlipUVs |
         aiProcess_CalcTangentSpace | aiProcess_FixInfacingNormals;
}

std::shared_ptr<AssimpSceneData> load_scene(
    const std::filesystem::path& input_file_path,
    const std::string& igasset_name, const std::shared_ptr<spdlog::logger>& log,
    Assimp::IOSystem* custom_io, std::string* error_out) {
  auto tr = std::make_shared<AssimpSceneData>();
  tr->importer = std::make_shared<Assimp::Importer>();
  tr->importer->SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);

  if (custom_io != nullptr) {
    // See the note in assimp-import.h: the Importer owns (and deletes) the
    // IOSystem from here on.
    tr->importer->SetIOHandler(custom_io);
  }

  // Use ReadFile (not ReadFileFromMemory) so that Assimp can resolve
  // external file references (glTF .bin buffers, .mtl files for OBJ, ...)
  // relative to the input asset. With a staged IOSystem, those references
  // resolve through the manifest instead of the real filesystem.
  tr->scene =
      tr->importer->ReadFile(input_file_path.string(), igasset_import_flags());

  if (!tr->scene) {
    const std::string error = tr->importer->GetErrorString();
    if (error_out != nullptr) {
      *error_out = error;
    }
    log->error("Failed to process geometry requested for {}: {}", igasset_name,
               error);
    return nullptr;
  }

  return tr;
}

}  // namespace stagedio