#include <igasset-gen/stb-parse.h>
#include <spdlog/spdlog.h>
#include <stb/stb_image.h>
#include <stb/stb_image_resize2.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

std::optional<StbImageData> StbImageData::blank_rgba(
    int width, int height, std::uint8_t default_color[4]) {
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }

  const std::size_t pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  uint8_t* buffer = static_cast<uint8_t*>(std::malloc(pixel_count * 4));
  if (buffer == nullptr) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < pixel_count; ++i) {
    buffer[i * 4 + 0] = default_color[0];
    buffer[i * 4 + 1] = default_color[1];
    buffer[i * 4 + 2] = default_color[2];
    buffer[i * 4 + 3] = default_color[3];
  }

  return StbImageData(4, width, height, buffer);
}

std::optional<StbImageData> StbImageData::from_memory(
    const std::string& enc_image, int force_channels) {
  int width = -1, height = -1, channels = -1;
  stbi_uc* img_buf =
      stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(enc_image.data()),
                            enc_image.size(), &width, &height, &channels,
                            force_channels);

  if (img_buf == nullptr) {
    auto log = spdlog::default_logger()->clone("StbImageData::from_memory");
    log->error("STB image unpacking failed");
    return std::nullopt;
  }

  int actual_channels = force_channels > 0 ? force_channels : channels;
  return StbImageData(actual_channels, width, height, img_buf);
}

StbImageData StbImageData::clone() const {
  const std::size_t byte_count = static_cast<std::size_t>(width) *
                                 static_cast<std::size_t>(height) *
                                 static_cast<std::size_t>(num_channels);
  uint8_t* cloned_data = static_cast<uint8_t*>(std::malloc(byte_count));
  std::memcpy(cloned_data, data, byte_count);
  return StbImageData(num_channels, width, height, cloned_data);
}

std::optional<StbImageData> StbImageData::crop(int x, int y, int w,
                                               int h) const {
  if (data == nullptr) {
    return std::nullopt;
  }
  if (w <= 0 || h <= 0) {
    return std::nullopt;
  }
  if (x < 0 || y < 0) {
    return std::nullopt;
  }
  if (x + w > width || y + h > height) {
    return std::nullopt;
  }

  const std::size_t row_bytes =
      static_cast<std::size_t>(w) * static_cast<std::size_t>(num_channels);
  uint8_t* cropped = static_cast<uint8_t*>(
      std::malloc(row_bytes * static_cast<std::size_t>(h)));
  if (cropped == nullptr) {
    return std::nullopt;
  }
  for (int row = 0; row < h; ++row) {
    const uint8_t* src_row = data + (static_cast<std::size_t>(y + row) *
                                         static_cast<std::size_t>(width) +
                                     static_cast<std::size_t>(x)) *
                                        static_cast<std::size_t>(num_channels);
    uint8_t* dst_row = cropped + static_cast<std::size_t>(row) * row_bytes;
    std::memcpy(dst_row, src_row, row_bytes);
  }

  return StbImageData(num_channels, w, h, cropped);
}

bool StbImageData::blit(const StbImageData& image, int x, int y, int width,
                        int height) {
  if (data == nullptr || image.data == nullptr) {
    return false;
  }
  if (width <= 0 || height <= 0) {
    return false;
  }
  if (image.num_channels != num_channels) {
    auto log = spdlog::default_logger()->clone("StbImageData");
    log->warn(
        "Blit error - channel count mismatch (this={} other={}); skipping blit",
        num_channels, image.num_channels);
    return false;
  }

  auto resized = image.resized(static_cast<uint16_t>(width),
                               static_cast<uint16_t>(height));
  if (!resized.has_value()) {
    return false;
  }

  // Clip the destination rect against this->{width,height}, tracking how many
  // pixels we've shifted into the (already-resized) source so the offsets stay
  // aligned when the user passes a partially-offscreen target.
  const int src_x_off = std::max(0, -x);
  const int src_y_off = std::max(0, -y);
  const int dst_x = std::max(0, x);
  const int dst_y = std::max(0, y);
  const int copy_w = std::min(width - src_x_off, this->width - dst_x);
  const int copy_h = std::min(height - src_y_off, this->height - dst_y);
  if (copy_w <= 0 || copy_h <= 0) {
    return false;
  }

  const std::size_t row_bytes =
      static_cast<std::size_t>(copy_w) * static_cast<std::size_t>(num_channels);
  for (int row = 0; row < copy_h; ++row) {
    uint8_t* dst_row = data + (static_cast<std::size_t>(dst_y + row) *
                                   static_cast<std::size_t>(this->width) +
                               static_cast<std::size_t>(dst_x)) *
                                  static_cast<std::size_t>(num_channels);
    const uint8_t* src_row =
        resized->data + (static_cast<std::size_t>(src_y_off + row) *
                             static_cast<std::size_t>(resized->width) +
                         static_cast<std::size_t>(src_x_off)) *
                            static_cast<std::size_t>(num_channels);
    std::memcpy(dst_row, src_row, row_bytes);
  }

  return true;
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
