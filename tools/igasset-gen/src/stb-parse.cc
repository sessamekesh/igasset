#include <igasset-gen/stb-parse.h>
#include <spdlog/spdlog.h>
#include <stb/stb_image.h>
#include <stb/stb_image_resize2.h>

#include <cstdint>
#include <optional>
#include <string>

namespace igassetgen {

StbImageData& StbImageData::operator=(StbImageData&& o) noexcept {
  if (this != &o) {
    if (data != nullptr) {
      stbi_image_free(data);
    }
    num_channels = o.num_channels;
    width = o.width;
    height = o.height;
    data = o.data;
    o.data = nullptr;
  }
  return *this;
}

StbImageData::~StbImageData() {
  if (data != nullptr) {
    stbi_image_free(data);
  }
}

std::optional<StbImageData> StbImageData::from_memory(
    const std::string& enc_image) {
  int width = -1, height = -1, channels = -1;
  stbi_uc* img_buf =
      stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(enc_image.data()),
                            enc_image.size(), &width, &height, &channels, 0);

  if (img_buf == nullptr) {
    auto log = spdlog::default_logger()->clone("StbImageData::from_memory");
    log->error("STB image unpacking failed");
    return std::nullopt;
  }

  return StbImageData(channels, width, height, img_buf);
}

StbImageData StbImageData::clone() const {
  uint8_t* cloned_data = new uint8_t[width * height * num_channels];
  memcpy(cloned_data, data, width * height * num_channels);
  return StbImageData(num_channels, width, height, cloned_data);
}

std::optional<StbImageData> StbImageData::resized(uint16_t new_width,
                                                  uint16_t new_height) const {
  if (width == new_width && height == new_height) {
    return clone();
  }

  stbir_pixel_layout layout{};
  if (num_channels == 3) {
    layout = stbir_pixel_layout::STBIR_RGB;
  } else if (num_channels == 4) {
    layout = stbir_pixel_layout::STBIR_RGBA;
  } else {
    return std::nullopt;
  }

  auto* resized_img = stbir_resize_uint8_linear(
      data, width, height,
      /* input_stride_in_bytes= */ 0,
      /* output_pixels= */ nullptr, new_width, new_height,
      /* output_stride_in_bytes= */ 0, layout);
  if (resized_img == nullptr) {
    return std::nullopt;
  }

  return StbImageData(num_channels, new_width, new_height, resized_img);
}

}  // namespace igassetgen
