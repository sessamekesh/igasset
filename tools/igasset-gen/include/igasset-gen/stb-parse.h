#ifndef IGASSET_GEN_STB_PARSE_H
#define IGASSET_GEN_STB_PARSE_H

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace igassetgen {

struct StbImageData {
 public:
  int num_channels;
  int width;
  int height;
  uint8_t* data;

  StbImageData() : num_channels(0), width(0), height(0), data(nullptr) {}
  StbImageData(const StbImageData&) = delete;
  StbImageData& operator=(const StbImageData&) = delete;
  StbImageData(StbImageData&& o) noexcept
      : num_channels(o.num_channels),
        width(o.width),
        height(o.height),
        data(o.data) {
    o.data = nullptr;
  }

  StbImageData& operator=(StbImageData&& o) noexcept;
  ~StbImageData();

  static std::optional<StbImageData> blank_rgba(int width, int height,
                                                std::uint8_t default_color[4]);
  static std::optional<StbImageData> from_memory(const std::string& enc_img,
                                                 int force_channels = 0);

  StbImageData clone() const;
  std::optional<StbImageData> crop(int x, int y, int w, int h) const;

  void premultiply_alpha();
  void unpremultiply_alpha();

  std::optional<StbImageData> resized(uint16_t new_width,
                                      uint16_t new_height) const;

  bool blit(const StbImageData& image, int x, int y, int width, int height);

 private:
  StbImageData(int num_channels, int width, int height, uint8_t* data)
      : num_channels(num_channels), width(width), height(height), data(data) {}
};

}  // namespace igassetgen

#endif
