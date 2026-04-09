#include <igasset/image2d.h>
#include <igasset/schema/igasset.h>
#include <spdlog/spdlog.h>

#if IGASSET_ENABLE_BASISU_SUPPORT
#include <transcoder/basisu_transcoder.h>

#include <mutex>
#endif

#include <optional>
#include <string>

namespace igasset {

static std::optional<Image2D> unpack_rgba8unorm(
    const IgAsset::Image2D* flatbuffer_data, Image2DFormat format) {
  if (flatbuffer_data->encoding() != IgAsset::Image2DEncoding_RGBA8Unorm) {
    return std::nullopt;
  }

  auto expected_format = Image2DFormat::RGBA8Unorm;

  if (expected_format != format) {
    auto log = spdlog::default_logger()->clone("Image2D");
    log->warn("Cannot unpack uncompressed data with mismatching formats");
    return std::nullopt;
  }

  std::string raw_data(flatbuffer_data->data()->size(), '\0');
  memcpy(raw_data.data(), flatbuffer_data->data()->data(),
         flatbuffer_data->data()->size());

  return Image2D(flatbuffer_data->width(), flatbuffer_data->height(), format,
                 std::move(raw_data));
}

#if IGASSET_ENABLE_BASISU_SUPPORT

static void maybe_init_basisu() {
  static bool basisu_initialized = false;
  static std::mutex m;

  {
    std::lock_guard l(m);
    if (!basisu_initialized) {
      basist::basisu_transcoder_init();
      basisu_initialized = true;
    }
  }
}

static basist::transcoder_texture_format get_transcoder_format(
    Image2DFormat fmt) {
  switch (fmt) {
    case Image2DFormat::RGBA8Unorm:
      return basist::transcoder_texture_format::cTFRGBA32;
    case Image2DFormat::BC7:
      return basist::transcoder_texture_format::cTFBC7_RGBA;
    case Image2DFormat::ASTC:
      return basist::transcoder_texture_format::cTFASTC_LDR_4x4_RGBA;
  }

  return basist::transcoder_texture_format::cTFTotalTextureFormats;
}

static std::optional<Image2D> unpack_basisu(
    const IgAsset::Image2D* flatbuffer_data, Image2DFormat format) {
  maybe_init_basisu();

  basist::basisu_transcoder transcoder;

  basist::basisu_image_info basisu_img_info{};
  if (!transcoder.get_image_info(flatbuffer_data->data()->data(),
                                 flatbuffer_data->data()->size(),
                                 basisu_img_info, 0)) {
    auto log = spdlog::default_logger()->clone("Image2D");
    log->error("Could not get image info for Basisu image");
    return std::nullopt;
  }

  uint32_t width = basisu_img_info.m_orig_width;
  uint32_t height = basisu_img_info.m_orig_height;

  if (!transcoder.start_transcoding(flatbuffer_data->data()->data(),
                                    flatbuffer_data->data()->size())) {
    auto log = spdlog::default_logger()->clone("Image2D");
    log->error("Could start Basisu transcoding");
    return std::nullopt;
  }

  auto basisu_format = get_transcoder_format(format);
  uint32_t bytes_per_block =
      basist::basis_get_bytes_per_block_or_pixel(basisu_format);
  uint32_t total_bytes = basist::basis_compute_transcoded_image_size_in_bytes(
      basisu_format, width, height);
  uint32_t total_blocks = total_bytes / bytes_per_block;

  std::string transcoded_data;
  transcoded_data.resize(total_bytes, '\0');

  if (!transcoder.transcode_image_level(
          flatbuffer_data->data()->data(), flatbuffer_data->data()->size(), 0,
          0, transcoded_data.data(), total_blocks, basisu_format)) {
    auto log = spdlog::default_logger()->clone("Image2D");
    log->error("Could transcode Basisu data!");
    return std::nullopt;
  }

  transcoder.stop_transcoding();

  return Image2D(width, height, format, std::move(transcoded_data));
}

#endif  // IGASSET_ENABLE_BASISU_SUPPORT

std::optional<Image2D> Image2D::Unpack(const IgAsset::Image2D* flatbuffer_data,
                                       Image2DFormat image_format) {
  switch (flatbuffer_data->encoding()) {
    case IgAsset::Image2DEncoding_RGBA8Unorm:
      return unpack_rgba8unorm(flatbuffer_data, image_format);
    case IgAsset::Image2DEncoding_ETC1S:
    case IgAsset::Image2DEncoding_UASTC_LDR_4_4:
#if IGASSET_ENABLE_BASISU_SUPPORT
      return unpack_basisu(flatbuffer_data, image_format);
#else
      auto log = spdlog::default_logger()->clone("Image2D");
      log->warn(
          "Cannot unpack unsupported encoding - build igasset with "
          "IGASSET_ENABLE_BASISU_SUPPORT to use");
      return std::nullopt;
#endif
  }

  auto log = spdlog::default_logger()->clone("Image2D");
  log->warn("Unpack - cannot extract unsupported encoding");
  return std::nullopt;
}

}  // namespace igasset
