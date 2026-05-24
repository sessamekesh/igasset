#include <flatbuffers/verifier.h>
#include <igasset/draco_dec.h>
#include <igasset/igpack_decoder.h>
#include <igasset/image2d.h>
#include <igasset/schema/igasset.h>
#include <igasset/spritesheet.h>
#include <igasset/vertex_types.h>
#include <igasset/wgsl_source.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr flatbuffers::Verifier::Options get_opts() {
  flatbuffers::Verifier::Options opts{};

  opts.assert = false;
  opts.check_alignment = false;
  opts.check_nested_flatbuffers = true;
  opts.max_depth = 8;
  opts.max_size = 512000000ull;

  return opts;
}

}  // namespace

namespace igasset {

std::shared_ptr<IgpackDecoder> IgpackDecoder::Create(std::string data) {
  const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(data.data());
  auto verifier = flatbuffers::Verifier(data_ptr, data.size(), ::get_opts());
  if (!IgAsset::VerifyAssetPackBuffer(verifier)) {
    SPDLOG_ERROR("Asset pack failed flatbuffer verification");
    return nullptr;
  }

  return std::shared_ptr<IgpackDecoder>(new IgpackDecoder(std::move(data)));
}

std::variant<igasset::WgslSource, IgpackExtractError>
IgpackDecoder::extract_wgl_shader(const std::string& asset_name) const {
  for (const auto* asset : *asset_pack_->assets()) {
    if (asset->name()->str() != asset_name) {
      continue;
    }

    if (asset->data_type() != IgAsset::SingleAssetData_WgslSource) {
      return IgpackExtractError::WrongResourceType;
    }

    return WgslSource::Unpack(asset->data_as_WgslSource());
  }

  return IgpackExtractError::ResourceNotFound;
}

std::variant<DracoDecoder, IgpackExtractError>
IgpackDecoder::extract_draco_decoder(const std::string& asset_name) const {
  for (const auto* asset : *asset_pack_->assets()) {
    if (asset->name()->str() != asset_name) {
      continue;
    }

    if (asset->data_type() != IgAsset::SingleAssetData_DracoGeometry) {
      return IgpackExtractError::WrongResourceType;
    }

    const auto* draco_asset = asset->data_as_DracoGeometry();

    std::vector<std::string> bone_names;
    std::vector<glm::mat4> bone_inv_bind_poses;

    for (const auto* ozz_bone_name : *draco_asset->ozz_bone_names()) {
      bone_names.push_back(ozz_bone_name->str());
    }

    for (const auto* ozz_inv_bind_pose : *draco_asset->ozz_inv_bind_poses()) {
      glm::mat4 inv_bind_pose(1.f);

      for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
          inv_bind_pose[r][c] = ozz_inv_bind_pose->values()->Get(r * 4 + c);
        }
      }

      bone_inv_bind_poses.push_back(inv_bind_pose);
    }

    auto decode_rsl = DracoDecoder::Create(
        reinterpret_cast<const char*>(draco_asset->draco_bin()->Data()),
        draco_asset->draco_bin()->size(), draco_asset->pos_attrib(),
        draco_asset->normal_attrib(),
        draco_asset->index_format() == IgAsset::IndexFormat_Uint16
            ? IndexBufferType::Uint16
            : IndexBufferType::Uint32,
        std::move(bone_names), std::move(bone_inv_bind_poses),
        draco_asset->tangent_attrib(), draco_asset->bitangent_attrib(),
        draco_asset->texcoord_attrib(), draco_asset->bone_idx_attrib(),
        draco_asset->bone_weight_attrib());
    if (std::holds_alternative<igasset::DracoDecoderError>(decode_rsl)) {
      auto log = spdlog::default_logger()->clone("IgpackDecoder");
      log->error(
          "Draco decoding failure for {}: {} - {}", asset_name,
          igasset::to_string(
              std::get<igasset::DracoDecoderError>(decode_rsl).error_type),
          std::get<igasset::DracoDecoderError>(decode_rsl).err_message);
      return IgpackExtractError::AssetExtractError;
    }

    return std::get<DracoDecoder>(std::move(decode_rsl));
  }

  return IgpackExtractError::ResourceNotFound;
}

std::variant<Image2D, IgpackExtractError> IgpackDecoder::extract_image2d(
    const std::string& asset_name, Image2DFormat format) const {
  for (const auto* asset : *asset_pack_->assets()) {
    if (asset->name()->str() != asset_name) {
      continue;
    }

    if (asset->data_type() != IgAsset::SingleAssetData_Image2D) {
      return IgpackExtractError::WrongResourceType;
    }

    auto image = Image2D::Unpack(asset->data_as_Image2D(), format);
    if (!image.has_value()) {
      auto log = spdlog::default_logger()->clone("IgpackDecoder");
      log->error("Image2D asset {} failed to unpack - invalid format or data",
                 asset_name);
      return IgpackExtractError::AssetExtractError;
    }

    return *std::move(image);
  }

  return IgpackExtractError::ResourceNotFound;
}

std::variant<Spritesheet, IgpackExtractError>
IgpackDecoder::extract_spritesheet(const std::string& asset_name,
                                   Image2DFormat format) const {
  for (const auto* asset : *asset_pack_->assets()) {
    if (asset->name()->str() != asset_name) {
      continue;
    }

    if (asset->data_type() != IgAsset::SingleAssetData_Spritesheet) {
      return IgpackExtractError::WrongResourceType;
    }

    auto spritesheet =
        Spritesheet::Unpack(asset->data_as_Spritesheet(), format);

    if (!spritesheet.has_value()) {
      auto log = spdlog::default_logger()->clone("IgpackDecoder");
      log->error(
          "Spritesheet asset {} failed to unpack - invalid format or data",
          asset_name);
      return IgpackExtractError::AssetExtractError;
    }

    return *std::move(spritesheet);
  }

  return IgpackExtractError::AssetExtractError;
}

}  // namespace igasset
