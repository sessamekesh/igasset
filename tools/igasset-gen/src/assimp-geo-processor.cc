#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <draco/attributes/geometry_attribute.h>
#include <draco/attributes/geometry_indices.h>
#include <draco/compression/encode.h>
#include <draco/core/data_buffer.h>
#include <draco/core/draco_types.h>
#include <draco/core/encoder_buffer.h>
#include <draco/draco_features.h>
#include <draco/mesh/mesh.h>
#include <flatbuffers/buffer.h>
#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/string.h>
#include <igasset-gen/assimp-geo-processor.h>
#include <igasset-gen/assimp-util.h>
#include <igasset-gen/inc-build.h>
#include <igasset-gen/schema/igasset-gen-plan.h>
#include <igasset/schema/igasset.h>
#include <igasset/vertex_types.h>
#include <igasync/promise.h>

#include <algorithm>
#include <assimp/Importer.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <ios>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

enum class LoadAssimpFileAltResult {
  UseCached,
  FsErr,
  EmptyBin,
  AssimpParseError,
  UnexpectedModelDataError,
  EncodingFailure,
};

struct BoneMeta {
  std::string name;
  glm::mat4 invBindPose;
};

struct BaseGeoData {
  std::vector<igasset::PositionNormalVertexData3D> posNormData;
  std::vector<igasset::TangentBitangentVertexData3D> tbData;
  std::vector<igasset::TexcoordVertexData> texcoordData;
  std::vector<igasset::BoneWeightsVertexData> boneWeightsData;
  std::vector<BoneMeta> boneMetadata;
  std::vector<uint32_t> indexData;
};

glm::mat4 aimat4_to_glm(const aiMatrix4x4& m) {
  return glm::mat4(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3, m.b3,
                   m.c3, m.d3, m.a4, m.b4, m.c4, m.d4);
}

const aiNode* find_node_for_mesh(const aiScene* scene, const aiNode* node,
                                 const aiMesh* mesh) {
  for (unsigned int i = 0; i < node->mNumMeshes; i++) {
    if (scene->mMeshes[node->mMeshes[i]] == mesh) {
      return node;
    }
  }
  for (unsigned int i = 0; i < node->mNumChildren; i++) {
    const aiNode* result = find_node_for_mesh(scene, node->mChildren[i], mesh);
    if (result) {
      return result;
    }
  }
  return nullptr;
}

glm::mat4 compute_world_transform(const aiNode* node) {
  glm::mat4 transform(1.0f);
  const aiNode* current = node;
  while (current != nullptr) {
    transform = aimat4_to_glm(current->mTransformation) * transform;
    current = current->mParent;
  }
  return transform;
}

struct MeshLoadResult {
  const aiMesh* mesh;
  glm::mat4 worldTransform;
};

std::shared_ptr<BaseGeoData> extract_base_geo(
    const MeshLoadResult& mesh_result,
    const std::shared_ptr<spdlog::logger>& log) {
  const aiMesh* mesh = mesh_result.mesh;
  const glm::mat4& world_transform = mesh_result.worldTransform;

  std::vector<igasset::PositionNormalVertexData3D> pos_norm_data;
  std::vector<igasset::TangentBitangentVertexData3D> tb_data;
  std::vector<igasset::TexcoordVertexData> texcoord_data;
  std::vector<igasset::BoneWeightsVertexData> bone_weights_data;
  std::vector<BoneMeta> bone_metadata;
  std::vector<uint32_t> index_data;

  pos_norm_data.reserve(mesh->mNumVertices);
  tb_data.reserve(mesh->mNumVertices);
  texcoord_data.reserve(mesh->mNumVertices);
  bone_weights_data.reserve(mesh->mNumVertices);
  bone_metadata.reserve(mesh->mNumBones);
  index_data.reserve(mesh->mNumFaces * 3);

  glm::mat3 upper3x3(world_transform);
  glm::mat3 normal_matrix = glm::transpose(glm::inverse(upper3x3));

  for (std::uint32_t idx = 0; idx < mesh->mNumVertices; idx++) {
    glm::vec3 raw_pos(mesh->mVertices[idx].x, mesh->mVertices[idx].y,
                      mesh->mVertices[idx].z);
    glm::vec3 raw_normal(mesh->mNormals[idx].x, mesh->mNormals[idx].y,
                         mesh->mNormals[idx].z);

    igasset::PositionNormalVertexData3D data{};
    data.Position = glm::vec3(world_transform * glm::vec4(raw_pos, 1.0f));
    data.Normal = glm::normalize(normal_matrix * raw_normal);

    if (mesh->HasTangentsAndBitangents()) {
      glm::vec3 raw_tangent(mesh->mTangents[idx].x, mesh->mTangents[idx].y,
                            mesh->mTangents[idx].z);
      glm::vec3 raw_bitangent(mesh->mBitangents[idx].x,
                              mesh->mBitangents[idx].y,
                              mesh->mBitangents[idx].z);

      igasset::TangentBitangentVertexData3D tb{};
      tb.Tangent = glm::normalize(upper3x3 * raw_tangent);
      tb.Bitangent = glm::normalize(upper3x3 * raw_bitangent);
      tb_data.push_back(tb);
    }

    pos_norm_data.push_back(data);

    if (mesh->HasTextureCoords(0)) {
      igasset::TexcoordVertexData tc_data{};
      tc_data.Texcoord.x = mesh->mTextureCoords[0][idx].x;
      tc_data.Texcoord.y = mesh->mTextureCoords[0][idx].y;
      texcoord_data.push_back(tc_data);
    }

    if (mesh->HasBones()) {
      igasset::BoneWeightsVertexData bw_data{};
      bw_data.Indices = {0u, 0u, 0u, 0u};
      bw_data.Weights = {0.f, 0.f, 0.f, 0.f};
      bone_weights_data.push_back(bw_data);
    }
  }

  for (std::uint32_t idx = 0; idx < mesh->mNumFaces; idx++) {
    if (mesh->mFaces[idx].mNumIndices != 3) {
      log->error("Non-triangular face encountered in mesh {}",
                 mesh->mName.C_Str());
      return nullptr;
    }
    index_data.push_back(mesh->mFaces[idx].mIndices[0]);
    index_data.push_back(mesh->mFaces[idx].mIndices[1]);
    index_data.push_back(mesh->mFaces[idx].mIndices[2]);
  }

  if (mesh->HasBones()) {
    for (std::uint32_t bone_idx = 0; bone_idx < mesh->mNumBones; bone_idx++) {
      const auto& bone = mesh->mBones[bone_idx];

      std::uint32_t weights_to_process = bone->mNumWeights;
      for (std::uint32_t widx = 0; widx < weights_to_process; widx++) {
        const auto& weight = bone->mWeights[widx];
        auto& o_vertex = bone_weights_data[weight.mVertexId];

        if (o_vertex.Weights[0] <= 0.f) {
          o_vertex.Indices[0] = bone_idx;
          o_vertex.Weights[0] = weight.mWeight;
        } else if (o_vertex.Weights[1] <= 0.f) {
          o_vertex.Indices[1] = bone_idx;
          o_vertex.Weights[1] = weight.mWeight;
        } else if (o_vertex.Weights[2] <= 0.f) {
          o_vertex.Indices[2] = bone_idx;
          o_vertex.Weights[2] = weight.mWeight;
        } else if (o_vertex.Weights[3] <= 0.f) {
          o_vertex.Indices[3] = bone_idx;
          o_vertex.Weights[3] = weight.mWeight;
        } else {
          log->error(
              "Error compiling bone associations - vertex {} has too many bone "
              "associations",
              weight.mVertexId);
          return nullptr;
        }
      }

      // Export bone data...
      const auto& inv = bone->mOffsetMatrix;
      glm::mat4 o_inv{};
      for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
          o_inv[r][c] = inv[c][r];
        }
      }
      bone_metadata.push_back(BoneMeta{bone->mName.C_Str(), o_inv});
    }
  }

  std::shared_ptr<BaseGeoData> out = std::make_shared<BaseGeoData>();
  out->posNormData = std::move(pos_norm_data);
  out->tbData = std::move(tb_data);
  out->texcoordData = std::move(texcoord_data);
  out->boneWeightsData = std::move(bone_weights_data);
  out->boneMetadata = std::move(bone_metadata);
  out->indexData = std::move(index_data);
  return out;
}

MeshLoadResult load_mesh_from_scene(
    std::shared_ptr<igassetgen::AssimpSceneData> maybe_scene_data,
    const std::string& mesh_name) {
  if (!maybe_scene_data) {
    return {nullptr, glm::mat4(1.0f)};
  }

  const aiScene* scene = maybe_scene_data->scene;

  for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
    const aiMesh* mesh = scene->mMeshes[i];

    // Optimization: skip all non-TRIANGLES (Assimp often imports
    // LINE types first)
    if (mesh->mPrimitiveTypes != aiPrimitiveType_TRIANGLE) {
      continue;
    }

    if (mesh->mName.C_Str() == mesh_name) {
      glm::mat4 world_transform(1.0f);
      const aiNode* node = find_node_for_mesh(scene, scene->mRootNode, mesh);
      if (node) {
        world_transform = compute_world_transform(node);
      }
      return {mesh, world_transform};
    }
  }

  return {nullptr, glm::mat4(1.0f)};
}

}  // namespace

namespace igassetgen {

std::shared_ptr<igasync::Promise<bool>>
AssimpGeoProcessor::draco_asset_from_assimp(
    const IgAssetGen::AssimpToDracoAction* action) const {
  if (action->assimp_mesh_names() == nullptr ||
      action->assimp_mesh_names()->size() == 0) {
    log_->error("No mesh names provided, cannot convert static geo {}",
                action->output_file_path()->str());
    return igasync::Promise<bool>::Immediate(false);
  }

  auto input_file_path =
      config_.InputAssetPathRoot / action->input_file_path()->str();

  log_->info("Extracting DRACO assets from {}",
             action->input_file_path()->str());

  return io_task_list_
      ->run([filesystem = filesystem_, config = config_, log = log_,
             input_file_path,
             action]() -> std::optional<LoadAssimpFileAltResult> {
        std::vector<std::filesystem::path> input_paths;
        input_paths.push_back(input_file_path);
        std::filesystem::path output_file_path =
            config.IgassetPathRoot / action->output_file_path()->str();
        if (skip_rebuild(config, std::move(input_paths), output_file_path,
                         log)) {
          return LoadAssimpFileAltResult::UseCached;
        }

        auto rsl = filesystem->read_bin(input_file_path);
        if (!rsl.has_value()) {
          return LoadAssimpFileAltResult::FsErr;
        }
        if (rsl->empty()) {
          return LoadAssimpFileAltResult::EmptyBin;
        }
        return std::nullopt;
      })
      ->then_consuming(
          [action, input_file_path,
           log = log_](std::optional<LoadAssimpFileAltResult> check_result)
              -> std::variant<std::shared_ptr<flatbuffers::FlatBufferBuilder>,
                              LoadAssimpFileAltResult> {
            //
            // ASSIMP parsing...
            if (check_result.has_value()) {
              return *check_result;
            }

            auto assimp_scene = load_scene(
                input_file_path, action->output_file_path()->str(), log);
            if (assimp_scene == nullptr) {
              log->error("Could not load scene for {}",
                         action->output_file_path()->str());
              return LoadAssimpFileAltResult::AssimpParseError;
            }

            uint32_t index_offset = 0u;
            bool has_texcoords = false;
            bool has_bones = false;
            uint32_t num_vertices = 0u;
            uint32_t num_faces = 0u;

            //
            // Mesh validation...
            for (int i = 0; i < action->assimp_mesh_names()->size(); i++) {
              std::string mesh_name =
                  action->assimp_mesh_names()->Get(i)->str();

              auto mesh_result =
                  ::load_mesh_from_scene(assimp_scene, mesh_name);
              const aiMesh* mesh = mesh_result.mesh;
              if (mesh == nullptr) {
                log->error("Mesh {} not found in scene for {}", mesh_name,
                           action->output_file_path()->str());
                return LoadAssimpFileAltResult::UnexpectedModelDataError;
              }

              if (i == 0) {
                has_texcoords = mesh->HasTextureCoords(0);
                has_bones = mesh->HasBones();
              } else {
                if (has_texcoords != mesh->HasTextureCoords(0)) {
                  log->error(
                      "Texcoord presence mismatch on {} - mesh {} has value "
                      "{}, "
                      "which does not match mesh 0",
                      action->output_file_path()->str(), i,
                      mesh->HasTextureCoords(0));
                  return LoadAssimpFileAltResult::UnexpectedModelDataError;
                }
                if (has_bones != mesh->HasBones()) {
                  log->error(
                      "Bone presence mismatch on {} - mesh {} has value {}, "
                      "which "
                      "does not match mesh 0",
                      action->output_file_path()->str(), i, mesh->HasBones());
                  return LoadAssimpFileAltResult::UnexpectedModelDataError;
                }
              }
              num_vertices += mesh->mNumVertices;
              num_faces += mesh->mNumFaces;
            }

            //
            // Validation finished! Extract geometry data...
            std::vector<igasset::PositionNormalVertexData3D> pos_norm_data;
            std::vector<igasset::TangentBitangentVertexData3D> tb_data;
            std::vector<igasset::TexcoordVertexData> texcoord_data;
            std::vector<igasset::BoneWeightsVertexData> bone_data;
            std::vector<BoneMeta> bone_meta;
            std::vector<std::uint32_t> index_data;
            pos_norm_data.reserve(num_vertices);
            tb_data.reserve(num_vertices);
            texcoord_data.reserve(has_texcoords ? num_vertices : 1u);
            bone_data.reserve(has_bones ? num_vertices : 1u);
            bone_meta.reserve(has_bones ? num_vertices : 1u);
            index_data.reserve(num_faces * 3u);

            for (int i = 0; i < action->assimp_mesh_names()->size(); i++) {
              std::string mesh_name =
                  action->assimp_mesh_names()->Get(i)->str();

              auto mesh_result =
                  ::load_mesh_from_scene(assimp_scene, mesh_name);
              if (mesh_result.mesh == nullptr) {
                log->error("Mesh {} not found in scene for {}", mesh_name,
                           action->output_file_path()->str());
                return LoadAssimpFileAltResult::UnexpectedModelDataError;
              }

              auto geo_data = ::extract_base_geo(mesh_result, log);
              if (geo_data == nullptr) {
                log->error(
                    "Failed to extract relevant static geo data for {} -- {}",
                    action->output_file_path()->str(), mesh_name);
                return LoadAssimpFileAltResult::UnexpectedModelDataError;
              }
              for (int j = 0; j < geo_data->posNormData.size(); j++) {
                pos_norm_data.push_back(geo_data->posNormData[j]);
              }
              for (int j = 0; j < geo_data->tbData.size(); j++) {
                tb_data.push_back(geo_data->tbData[j]);
              }
              for (int j = 0; j < geo_data->texcoordData.size(); j++) {
                texcoord_data.push_back(geo_data->texcoordData[j]);
              }
              for (int j = 0; j < geo_data->boneWeightsData.size(); j++) {
                bone_data.push_back(geo_data->boneWeightsData[j]);
              }
              for (int j = 0; j < geo_data->indexData.size(); j++) {
                index_data.push_back(geo_data->indexData[j] + index_offset);
              }
              index_offset += mesh_result.mesh->mNumVertices;

              if (i == 0) {
                bone_meta = geo_data->boneMetadata;
              } else {
                if (bone_meta.size() != geo_data->boneMetadata.size()) {
                  log->error(
                      "Bone metadata mismatch, cannot extract bone bindings "
                      "for {}",
                      mesh_name);
                  return LoadAssimpFileAltResult::UnexpectedModelDataError;
                }
                for (int j = 0; j < geo_data->boneMetadata.size(); j++) {
                  if (bone_meta[j].name != geo_data->boneMetadata[j].name) {
                    log->error(
                        "Bone name mismatch in mesh {} at bone {} (expected "
                        "{})",
                        mesh_name, geo_data->boneMetadata[j].name,
                        bone_meta[j].name);
                    return LoadAssimpFileAltResult::UnexpectedModelDataError;
                  }
                }
              }
            }

            if (index_data.size() % 3 != 0) {
              log->error("Invalid number of triangle indices {} for {}",
                         index_data.size(), action->output_file_path()->str());
              return LoadAssimpFileAltResult::UnexpectedModelDataError;
            }

            if (index_data.size() == 0) {
              log->error("No faces found for {}",
                         action->output_file_path()->str());
              return LoadAssimpFileAltResult::UnexpectedModelDataError;
            }

            //
            // Draco encoding...
            draco::Mesh draco_mesh;
            draco_mesh.set_num_points(pos_norm_data.size());

            draco::DataBuffer pos_norm_data_buffer;
            pos_norm_data_buffer.Update(
                &pos_norm_data[0],
                pos_norm_data.size() *
                    sizeof(igasset::PositionNormalVertexData3D));

            int pos_attribute_idx = -1;
            int normal_attribute_idx = -1;
            int tangent_attribute_idx = -1;
            int bitangent_attribute_idx = -1;
            int texcoord_attribute_idx = -1;
            int bone_idx_attrib_idx = -1;
            int bone_weight_attrib_idx = -1;

            // Position attribute
            {
              draco::GeometryAttribute pos_attribute;
              pos_attribute.Init(
                  draco::GeometryAttribute::POSITION, &pos_norm_data_buffer, 3,
                  draco::DT_FLOAT32, false,
                  sizeof(igasset::PositionNormalVertexData3D),
                  offsetof(igasset::PositionNormalVertexData3D, Position));
              pos_attribute_idx = draco_mesh.AddAttribute(pos_attribute, true,
                                                          pos_norm_data.size());
              if (pos_attribute_idx < 0) {
                log->error("Failed to set up Draco position attribute for {}",
                           action->output_file_path()->str());
                return LoadAssimpFileAltResult::EncodingFailure;
              }
            }

            // Normal attribute
            {
              draco::GeometryAttribute normal_attribute;
              normal_attribute.Init(
                  draco::GeometryAttribute::NORMAL, &pos_norm_data_buffer, 3,
                  draco::DT_FLOAT32, false,
                  sizeof(igasset::PositionNormalVertexData3D),
                  offsetof(igasset::PositionNormalVertexData3D, Normal));
              normal_attribute_idx = draco_mesh.AddAttribute(
                  normal_attribute, true, pos_norm_data.size());
              if (normal_attribute_idx < 0) {
                log->error("Failed to set up Draco normal attribute for {}",
                           action->output_file_path()->str());
                return LoadAssimpFileAltResult::EncodingFailure;
              }
            }

            // Set mappings...
            for (uint32_t i = 0u; i < pos_norm_data.size(); i++) {
              draco_mesh.attribute(pos_attribute_idx)
                  ->SetAttributeValue(draco::AttributeValueIndex(i),
                                      &pos_norm_data[i].Position);
              draco_mesh.attribute(normal_attribute_idx)
                  ->SetAttributeValue(draco::AttributeValueIndex(i),
                                      &pos_norm_data[i].Normal);
            }

            if (action->include_tangent_bitangents()) {
              draco::DataBuffer tb_data_buffer;
              tb_data_buffer.Update(
                  &tb_data[0],
                  tb_data.size() *
                      sizeof(igasset::TangentBitangentVertexData3D));
              {
                draco::GeometryAttribute tangent_attribute;
                tangent_attribute.Init(
                    draco::GeometryAttribute::GENERIC, &tb_data_buffer, 3,
                    draco::DT_FLOAT32, false,
                    sizeof(igasset::TangentBitangentVertexData3D),
                    offsetof(igasset::TangentBitangentVertexData3D, Tangent));
                tangent_attribute_idx = draco_mesh.AddAttribute(
                    tangent_attribute, true, tb_data.size());
                if (tangent_attribute_idx < 0) {
                  log->error("Failed to set up Draco tangent attribute for {}",
                             action->output_file_path()->str());
                  return LoadAssimpFileAltResult::EncodingFailure;
                }
              }

              {
                draco::GeometryAttribute bitangent_attribute;
                bitangent_attribute.Init(
                    draco::GeometryAttribute::GENERIC, &tb_data_buffer, 3,
                    draco::DT_FLOAT32, false,
                    sizeof(igasset::TangentBitangentVertexData3D),
                    offsetof(igasset::TangentBitangentVertexData3D, Bitangent));
                bitangent_attribute_idx = draco_mesh.AddAttribute(
                    bitangent_attribute, true, tb_data.size());
                if (bitangent_attribute_idx < 0) {
                  log->error(
                      "Failed to set up Draco bitangent attribute for {}",
                      action->output_file_path()->str());
                  return LoadAssimpFileAltResult::EncodingFailure;
                }
              }
            }

            // texcoord attribute
            if (has_texcoords && action->include_texcoords()) {
              draco::DataBuffer texcoord_databuffer;
              texcoord_databuffer.Update(
                  &texcoord_data[0],
                  texcoord_data.size() * sizeof(igasset::TexcoordVertexData));

              draco::GeometryAttribute uv_attribute;
              uv_attribute.Init(
                  draco::GeometryAttribute::TEX_COORD, &texcoord_databuffer, 2,
                  draco::DT_FLOAT32, false, sizeof(igasset::TexcoordVertexData),
                  offsetof(igasset::TexcoordVertexData, Texcoord));

              texcoord_attribute_idx = draco_mesh.AddAttribute(
                  uv_attribute, true, texcoord_data.size());
              for (int i = 0; i < texcoord_data.size(); i++) {
                draco_mesh.attribute(texcoord_attribute_idx)
                    ->SetAttributeValue(draco::AttributeValueIndex(i),
                                        &texcoord_data[i].Texcoord);
              }
              if (texcoord_attribute_idx < 0) {
                log->error("Failed to set up Draco texcoord attribute for {}",
                           action->output_file_path()->str());
                return LoadAssimpFileAltResult::EncodingFailure;
              }
            }

            if (has_bones && action->include_bones()) {
              draco::DataBuffer bone_data_buffer;
              bone_data_buffer.Update(
                  &bone_data[0],
                  bone_data.size() * sizeof(igasset::BoneWeightsVertexData));

              draco::GeometryAttribute bone_idx_attrib;
              bone_idx_attrib.Init(
                  draco::GeometryAttribute::GENERIC, &bone_data_buffer, 4,
                  draco::DT_UINT8, false,
                  sizeof(igasset::BoneWeightsVertexData),
                  offsetof(igasset::BoneWeightsVertexData, Indices));

              bone_idx_attrib_idx = draco_mesh.AddAttribute(
                  bone_idx_attrib, true, bone_data.size());

              draco::GeometryAttribute bone_weight_attrib;
              bone_weight_attrib.Init(
                  draco::GeometryAttribute::GENERIC, &bone_data_buffer, 4,
                  draco::DT_FLOAT32, false,
                  sizeof(igasset::BoneWeightsVertexData),
                  offsetof(igasset::BoneWeightsVertexData, Weights));

              bone_weight_attrib_idx = draco_mesh.AddAttribute(
                  bone_weight_attrib, true, bone_data.size());

              for (int i = 0; i < bone_data.size(); i++) {
                draco_mesh.attribute(bone_idx_attrib_idx)
                    ->SetAttributeValue(draco::AttributeValueIndex(i),
                                        &bone_data[i].Indices);
                draco_mesh.attribute(bone_weight_attrib_idx)
                    ->SetAttributeValue(draco::AttributeValueIndex(i),
                                        &bone_data[i].Weights);
              }

              if (bone_idx_attrib_idx < 0) {
                log->error("Failed to set up Draco bone idx attribute for {}",
                           action->output_file_path()->str());
                return LoadAssimpFileAltResult::EncodingFailure;
              }
              if (bone_weight_attrib_idx < 0) {
                log->error(
                    "Failed to set up Draco bone weight attribute for {}",
                    action->output_file_path()->str());
                return LoadAssimpFileAltResult::EncodingFailure;
              }
            }

            // faces
            draco_mesh.SetNumFaces(index_data.size() / 3);
            for (int i = 0; i < index_data.size() / 3; i++) {
              draco::Mesh::Face face;
              face[0] = index_data[i * 3];
              face[1] = index_data[i * 3 + 1];
              face[2] = index_data[i * 3 + 2];
              draco_mesh.SetFace(draco::FaceIndex(i), face);
            }

#ifdef DRACO_ATTRIBUTE_VALUES_DEDUPLICATION_SUPPORTED
            draco_mesh.DeduplicateAttributeValues();
#endif
#ifdef DRACO_ATTRIBUTE_INDICES_DEDUPLICATION_SUPPORTED
            draco_mesh.DeduplicatePointIds();
#endif

            draco::Encoder encoder;
            // draco_params is optional in the FlatBuffers schema; use defaults
            // matching DracoCompressionParams in igasset-gen-plan.fbs when
            // null.
            uint8_t compression_level = 10;
            uint8_t decompression_level = 10;
            uint8_t general_quantization = 11;
            uint8_t pos_quantization = 11;
            uint8_t normal_quantization = 11;
            uint8_t texcoord_quantization = 11;
            if (action->draco_params() != nullptr) {
              compression_level = action->draco_params()->compression_level();
              decompression_level =
                  action->draco_params()->decompression_level();
              general_quantization =
                  action->draco_params()->general_quantization();
              pos_quantization = action->draco_params()->pos_quantization();
              normal_quantization =
                  action->draco_params()->normal_quantization();
              texcoord_quantization =
                  action->draco_params()->texcoord_quantization();
            }

            int encoding_speed =
                std::min(10, std::max(0, 10 - compression_level));
            int decoding_speed =
                std::min(10, std::max(0, 10 - decompression_level));

            encoder.SetSpeedOptions(encoding_speed, decoding_speed);
            encoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION,
                                             pos_quantization);
            encoder.SetAttributeQuantization(
                draco::GeometryAttribute::TEX_COORD, texcoord_quantization);
            encoder.SetAttributeQuantization(draco::GeometryAttribute::GENERIC,
                                             general_quantization);
            encoder.SetAttributeQuantization(draco::GeometryAttribute::NORMAL,
                                             normal_quantization);

            draco::EncoderBuffer out_buffer;
            auto status = encoder.EncodeMeshToBuffer(draco_mesh, &out_buffer);
            if (!status.ok()) {
              log->error("Failure in Draco encoder for {}: {}",
                         action->output_file_path()->str(),
                         status.error_msg_string());
              return LoadAssimpFileAltResult::EncodingFailure;
            }

            //
            // Encode to igasset FlatBuffer
            auto fbb = std::make_shared<flatbuffers::FlatBufferBuilder>();

            auto index_type = (index_offset >= 0xFFFF)
                                  ? IgAsset::IndexFormat_Uint16
                                  : IgAsset::IndexFormat_Uint32;

            auto fb_draco_bin = fbb->CreateVector(
                reinterpret_cast<const uint8_t*>(out_buffer.data()),
                out_buffer.size());
            std::vector<flatbuffers::Offset<flatbuffers::String>> fb_bone_names;
            std::vector<flatbuffers::Offset<IgAsset::Mat4>> fb_inv_bind_poses;
            for (int i = 0; i < bone_meta.size(); i++) {
              fb_bone_names.push_back(fbb->CreateString(bone_meta[i].name));
              auto fb_inv_bind_pose_vec = fbb->CreateVector(
                  &bone_meta[i].invBindPose[0][0], 16 * sizeof(float));
              auto fb_inv_bind_pose_mat4 =
                  IgAsset::CreateMat4(*fbb, fb_inv_bind_pose_vec);
              fb_inv_bind_poses.push_back(fb_inv_bind_pose_mat4);
            }

            auto fb_ozz_bone_names = fbb->CreateVector(fb_bone_names);
            auto fb_ozz_inv_bind_poses = fbb->CreateVector(fb_inv_bind_poses);

            auto fb_draco_geo = IgAsset::CreateDracoGeometry(
                *fbb, pos_attribute_idx, normal_attribute_idx,
                tangent_attribute_idx, bitangent_attribute_idx,
                texcoord_attribute_idx, bone_idx_attrib_idx,
                bone_weight_attrib_idx, fb_draco_bin, index_type,
                fb_ozz_bone_names, fb_ozz_inv_bind_poses);

            auto asset = IgAsset::CreateAsset(
                *fbb, IgAsset::SingleAssetData_DracoGeometry,
                fb_draco_geo.Union());

            fbb->Finish(asset);
            return fbb;
          },
          exec_task_list_)
      ->then_consuming(
          [config = config_, log = log_, action, filesystem = filesystem_](
              std::variant<std::shared_ptr<flatbuffers::FlatBufferBuilder>,
                           LoadAssimpFileAltResult>
                  process_result) -> bool {
            if (std::holds_alternative<LoadAssimpFileAltResult>(
                    process_result)) {
              switch (std::get<LoadAssimpFileAltResult>(process_result)) {
                case LoadAssimpFileAltResult::UseCached:
                  return true;
                default:
                  return false;
              }
            }

            std::shared_ptr<flatbuffers::FlatBufferBuilder> draco_igasset_bin =
                std::get<std::shared_ptr<flatbuffers::FlatBufferBuilder>>(
                    std::move(process_result));

            return filesystem->write_bin(
                config.IgassetPathRoot / action->output_file_path()->str(),
                *draco_igasset_bin);
          },
          io_task_list_);

  return nullptr;
}

}  // namespace igassetgen