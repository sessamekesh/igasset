#include <igasset/generators/plane.h>
#include <igasset/index_buffer.h>
#include <igasset/vertex_types.h>

#include <vector>

namespace igasset {

std::vector<PositionNormalVertexData3D> PlaneGenerator::get_pos_norm_verts()
    const {
  std::vector<PositionNormalVertexData3D> vertices;

  vertices.push_back(PositionNormalVertexData3D{
      .Position = position + glm::vec3{-width / 2.f, 0.f, -depth / 2.f},
      .Normal = normal,
  });
  vertices.push_back(PositionNormalVertexData3D{
      .Position = position + glm::vec3{width / 2.f, 0.f, -depth / 2.f},
      .Normal = normal,
  });
  vertices.push_back(PositionNormalVertexData3D{
      .Position = position + glm::vec3{-width / 2.f, 0.f, depth / 2.f},
      .Normal = normal,
  });
  vertices.push_back(PositionNormalVertexData3D{
      .Position = position + glm::vec3{width / 2.f, 0.f, depth / 2.f},
      .Normal = normal,
  });

  return vertices;
}

std::vector<TexcoordVertexData> PlaneGenerator::get_texcoord_verts() const {
  std::vector<TexcoordVertexData> vertices;

  vertices.push_back(TexcoordVertexData{.Texcoord = glm::vec2(-1.f, -1.f)});
  vertices.push_back(TexcoordVertexData{.Texcoord = glm::vec2(1.f, -1.f)});
  vertices.push_back(TexcoordVertexData{.Texcoord = glm::vec2(-1.f, 1.f)});
  vertices.push_back(TexcoordVertexData{.Texcoord = glm::vec2(1.f, 1.f)});

  return vertices;
}

IndexBuffer PlaneGenerator::get_index_buffer() const {
  return IndexBuffer::Builder(IndexBufferType::Uint16, 6)
      .add(0u)
      .add(2u)
      .add(1u)
      .add(1u)
      .add(2u)
      .add(3u)
      .build();
}

}  // namespace igasset
