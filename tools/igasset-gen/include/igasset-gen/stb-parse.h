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

  static std::optional<StbImageData> from_memory(const std::string& enc_img);

  StbImageData clone() const;

  std::optional<StbImageData> resized(uint16_t new_width,
                                      uint16_t new_height) const;

 private:
  StbImageData(int num_channels, int width, int height, uint8_t* data)
      : num_channels(num_channels), width(width), height(height), data(data) {}
};

}  // namespace igassetgen

#endif
