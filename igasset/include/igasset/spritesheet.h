#ifndef IGASSET_SPRITESHEET_H
#define IGASSET_SPRITESHEET_H

#include <igasset/image2d.h>
#include <igasset/schema/igasset.h>
#include <glm/glm.hpp>
#include <optional>
#include <unordered_map>

namespace igasset {

struct Sprite {
  glm::vec2 uv;
  glm::vec2 size;
};

struct Spritesheet {
  Spritesheet(Image2D image, std::unordered_map<std::string, Sprite> sprites)
      : image(std::move(image)), sprites(std::move(sprites)) {}

  Spritesheet(const Spritesheet&) = delete;
  Spritesheet& operator=(const Spritesheet&) = delete;
  Spritesheet(Spritesheet&&) = default;
  Spritesheet& operator=(Spritesheet&&) = default;
  ~Spritesheet() = default;

  Image2D image;
  std::unordered_map<std::string, Sprite> sprites;

  static std::optional<Spritesheet> Unpack(
      const IgAsset::Spritesheet* flatbuffer_data, Image2DFormat image_format);
};

}  // namespace igasset

#endif
