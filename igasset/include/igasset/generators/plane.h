#ifndef IGASSET_PLANE_GENERATOR_H
#define IGASSET_PLANE_GENERATOR_H

#include <igasset/index_buffer.h>
#include <igasset/vertex_types.h>

#include <glm/glm.hpp>
#include <vector>

namespace igasset {

struct PlaneGenerator {
  glm::vec3 position = glm::vec3{0.f, 0.f, 0.f};
  float width = 1.f;
  float depth = 1.f;

  std::vector<PositionNormalVertexData3D> get_pos_norm_verts() const;
  std::vector<TexcoordVertexData> get_texcoord_verts() const;
  IndexBuffer get_index_buffer() const;
};

}  // namespace igasset

#endif
