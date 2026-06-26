#include <igasset/image2d.h>
#include <igasset/spritesheet.h>
#include <spdlog/spdlog.h>

namespace igasset {

std::optional<Spritesheet> Spritesheet::Unpack(
    const IgAsset::Spritesheet* flatbuffer_data, Image2DFormat image_format) {
  auto fb_img = flatbuffer_data->image();
  auto fb_sprite_list = flatbuffer_data->sprites();

  if (fb_img == nullptr || fb_sprite_list == nullptr) {
    auto log = spdlog::default_logger()->clone("Spritesheet");
    log->warn(
        "Cannot unpack spritesheet - missing required data (image: {}, "
        "sprites: {})",
        fb_img == nullptr ? "missing" : "present",
        fb_sprite_list == nullptr ? "missing" : "present");
    return std::nullopt;
  }

  auto image = Image2D::Unpack(flatbuffer_data->image(), image_format);
  if (!image.has_value()) {
    auto log = spdlog::default_logger()->clone("Spritesheet");
    log->warn("Cannot unpack spritesheet - failed to unpack image");
    return std::nullopt;
  }

  std::unordered_map<std::string, Sprite> sprites;
  sprites.reserve(fb_sprite_list->size());

  for (auto* fb_sprite : *fb_sprite_list) {
    Sprite sprite{
        .uv = {static_cast<float>(fb_sprite->x()) /
                   static_cast<float>(flatbuffer_data->image()->width()),
               static_cast<float>(fb_sprite->y()) /
                   static_cast<float>(flatbuffer_data->image()->height())},
        .size = {static_cast<float>(fb_sprite->w()) /
                     static_cast<float>(flatbuffer_data->image()->width()),
                 static_cast<float>(fb_sprite->h()) /
                     static_cast<float>(flatbuffer_data->image()->height())},
    };

    auto fb_sprite_name = fb_sprite->name();
    if (fb_sprite_name == nullptr) {
      auto log = spdlog::default_logger()->clone("Spritesheet");
      log->warn(
          "Cannot unpack individual sprite at [{}, {}] {} x {} - no name given",
          sprite.uv.x, sprite.uv.y, sprite.size.x, sprite.size.y);
      continue;
    }

    auto name = fb_sprite_name->str();

    if (sprites.contains(name)) {
      auto log = spdlog::default_logger()->clone("Spritesheet");
      log->warn("Duplicate sprite with name {} found - skipping", name);
      continue;
    }

    sprites[name] = sprite;
  }

  return Spritesheet(std::move(*image), std::move(sprites));
}

}  // namespace igasset
