#ifndef IGASSET_ASSET_METADATA_H
#define IGASSET_ASSET_METADATA_H

#include <igasset/image2d.h>
#include <igasset/index_buffer.h>

#include <string>
#include <variant>
#include <vector>

namespace igasset {

struct WgslMetadata {
  bool has_vertex;
  bool has_fragment;
  bool has_compute;
  size_t wgsl_size;
};

struct DracoMetadata {
  bool has_pos_norm;
  bool has_texcoord;
  bool has_bones;
  IndexBufferType index_buffer_type;
  size_t buffer_size;
};

struct Image2DMetadata {
  std::string storage_format;
  uint32_t width;
  uint32_t height;
  size_t image_size;
};

struct SpritesheetMetadata {
  Image2DMetadata image_metadata;
  size_t num_sprites;
};

using AssetMetadata = std::variant<WgslMetadata, DracoMetadata, Image2DMetadata,
                                   SpritesheetMetadata>;

}  // namespace igasset

#endif