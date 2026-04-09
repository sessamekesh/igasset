#ifndef IGASSET_IMAGE_2D_H
#define IGASSET_IMAGE_2D_H

#include <igasset/schema/igasset.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace igasset {

enum class Image2DFormat {
  RGBA8Unorm,
  BC7,
  ASTC,
};

struct Image2D {
  uint32_t width;
  uint32_t height;
  Image2DFormat format;
  std::string data;

  Image2D(uint32_t width, uint32_t height, Image2DFormat format,
          std::string data)
      : width(width), height(height), format(format), data(std::move(data)) {}

  Image2D(const Image2D&) = delete;
  Image2D& operator=(const Image2D&) = delete;
  Image2D(Image2D&&) = default;
  Image2D& operator=(Image2D&&) = default;
  ~Image2D() = default;

  static std::optional<Image2D> Unpack(const IgAsset::Image2D* flatbuffer_data,
                                       Image2DFormat image_format);
};

}  // namespace igasset

#endif
