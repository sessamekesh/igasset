#ifndef IGASSET_DRACO_DEC_H_
#define IGASSET_DRACO_DEC_H_

#include <draco/compression/decode.h>
#include <igasset/index_buffer.h>
#include <igasset/vertex_types.h>

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace igasset {

enum class DracoDecoderErrorType {
  DecodeFailed,
  MeshDataMissing,
  MeshDataInvalid,
  NoData,
  DrcPointCloudIsNotMesh,
};

std::string to_string(DracoDecoderErrorType err_type);

struct DracoDecoderError {
  DracoDecoderErrorType error_type;
  std::string err_message;
};

class DracoDecoder {
 public:
  static std::variant<DracoDecoder, DracoDecoderError> Create(
      const char* draco_buffer, size_t draco_buffer_len, int pos_attrib,
      int normal_attrib, IndexBufferType index_buffer_type,
      std::vector<std::string> bone_names,
      std::vector<glm::mat4> bone_inv_bind_poses, int tangent_attrib = -1,
      int bitangent_attrib = -1, int texcoord_attrib = -1,
      int bone_idx_attrib = -1, int bone_weight_attrib = -1);

  std::variant<std::vector<igasset::PositionNormalVertexData3D>,
               DracoDecoderError>
  get_pos_norm_data() const;

  std::variant<std::vector<igasset::TexcoordVertexData>, DracoDecoderError>
  get_texcoord_data() const;

  std::variant<std::vector<igasset::BoneWeightsVertexData>, DracoDecoderError>
  get_bone_data() const;

  std::variant<std::vector<std::string>, DracoDecoderError> get_bone_names()
      const;

  std::variant<std::vector<glm::mat4>, DracoDecoderError>
  get_bone_inv_bind_poses() const;

  IndexBufferType index_buffer_type() const {
    return index_buffer_type_;
  }

  std::variant<IndexBuffer, DracoDecoderError> get_index_buffer() const;

  DracoDecoder() = delete;
  DracoDecoder(const DracoDecoder&) = delete;
  DracoDecoder& operator=(const DracoDecoder&) = delete;
  DracoDecoder(DracoDecoder&&) = default;
  DracoDecoder& operator=(DracoDecoder&&) = default;
  ~DracoDecoder();

 private:
  DracoDecoder(std::unique_ptr<draco::Mesh>, int position_attrib,
               int normal_attrib, int tangent_attrib, int bitangent_attrib,
               int texcoord_attrib, int bone_idx_attrib, int bone_weight_attrib,
               IndexBufferType index_buffer_type,
               std::vector<std::string> bone_names,
               std::vector<glm::mat4> bone_inv_bind_poses);
  std::unique_ptr<draco::Mesh> mesh_;
  int position_attrib_;
  int normal_attrib_;
  int tangent_attrib_;
  int bitangent_attrib_;
  int texcoord_attrib_;
  int bone_idx_attrib_;
  int bone_weight_attrib_;
  IndexBufferType index_buffer_type_;
  std::vector<std::string> bone_names_;
  std::vector<glm::mat4> bone_inv_bind_poses_;
};

}  // namespace igasset

#endif
