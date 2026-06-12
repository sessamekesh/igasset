#include <flatbuffers/verifier.h>
#include <igasset/asset_metadata.h>
#include <igasset/draco_dec.h>
#include <igasset/image2d.h>
#include <igasset/schema/igasset.h>
#include <igasset/schema/igpack.h>
#include <igasset/schema/types.h>
#include <igasset/single_asset_parser.h>
#include <igasset/spritesheet.h>
#include <igasset/wgsl_source.h>
#include <spdlog/spdlog-inl.h>
#include <cstddef>
#include <utility>
#include <variant>

namespace {
auto logger() {
  static auto log = spdlog::default_logger()->clone("IgpackDecoder");
  return log;
}

igasset::SingleAssetParser::FbsAssetT extract_from_single_asset(
    const IgAsset::SingleAsset* fbs_asset) {
  switch (fbs_asset->data_type()) {
    case IgAsset::SingleAssetData_WgslSource:
      return fbs_asset->data_as_WgslSource();
    case IgAsset::SingleAssetData_DracoGeometry:
      return fbs_asset->data_as_DracoGeometry();
    case IgAsset::SingleAssetData_Image2D:
      return fbs_asset->data_as_Image2D();
    case IgAsset::SingleAssetData_Spritesheet:
      return fbs_asset->data_as_Spritesheet();
    default:
      return nullptr;
  }

  std::unreachable();
}

igasset::SingleAssetParser::FbsAssetT extract_from_asset(
    const IgAsset::Asset* fbs_asset) {
  switch (fbs_asset->asset_type()) {
    case IgAsset::SingleAssetData_WgslSource:
      return fbs_asset->asset_as_WgslSource();
    case IgAsset::SingleAssetData_DracoGeometry:
      return fbs_asset->asset_as_DracoGeometry();
    case IgAsset::SingleAssetData_Image2D:
      return fbs_asset->asset_as_Image2D();
    case IgAsset::SingleAssetData_Spritesheet:
      return fbs_asset->asset_as_Spritesheet();
    default:
      return nullptr;
  }

  std::unreachable();
}

}  // namespace

namespace igasset {

SingleAssetParser::SingleAssetParser(const IgAsset::SingleAsset* fbs_asset)
    : fbs_asset_(::extract_from_single_asset(fbs_asset)) {}

SingleAssetParser::SingleAssetParser(const IgAsset::Asset* fbs_asset)
    : fbs_asset_(::extract_from_asset(fbs_asset)) {}

SingleAssetParser::~SingleAssetParser() {
  fbs_asset_ = nullptr;
}

AssetType SingleAssetParser::asset_type() const {
  if (std::holds_alternative<const IgAsset::WgslSource*>(fbs_asset_)) {
    return AssetType::WgslSource;
  } else if (std::holds_alternative<const IgAsset::DracoGeometry*>(
                 fbs_asset_)) {
    return AssetType::DracoGeo;
  } else if (std::holds_alternative<const IgAsset::Image2D*>(fbs_asset_)) {
    return AssetType::Image2D;
  } else if (std::holds_alternative<const IgAsset::Spritesheet*>(fbs_asset_)) {
    return AssetType::Spritesheet;
  }

  return AssetType::Unknown;
}

std::optional<std::vector<Image2DFormat>>
SingleAssetParser::compatible_image_formats() const {
  if (asset_type() != AssetType::Image2D) {
    return std::nullopt;
  }

  const auto* fbs_image2d = std::get<const IgAsset::Image2D*>(fbs_asset_);

  if (fbs_image2d == nullptr) {
    return std::nullopt;
  }

  switch (fbs_image2d->encoding()) {
    case IgAsset::Image2DEncoding_RGBA8Unorm:
      return std::vector<Image2DFormat>{Image2DFormat::RGBA8Unorm};
    case IgAsset::Image2DEncoding_ETC1S:
    case IgAsset::Image2DEncoding_UASTC_LDR_4_4:
      return std::vector<Image2DFormat>{
          Image2DFormat::RGBA8Unorm, Image2DFormat::ASTC, Image2DFormat::BC7};
    default:
      return std::nullopt;
  }

  std::unreachable();
}

std::optional<Image2DMetadata> image_metadata(
    const IgAsset::Image2D* fbs_image) {
  if (fbs_image == nullptr || fbs_image->data() == nullptr) {
    return std::nullopt;
  }

  std::string storage_format = "<< Unknown >>";
  switch (fbs_image->encoding()) {
    case IgAsset::Image2DEncoding::Image2DEncoding_ETC1S:
      storage_format = "ETC1S";
      break;
    case IgAsset::Image2DEncoding::Image2DEncoding_RGBA8Unorm:
      storage_format = "RGBA8Unorm";
      break;
    case IgAsset::Image2DEncoding::Image2DEncoding_UASTC_LDR_4_4:
      storage_format = "UASTC_LDR_4_4";
      break;
  }

  return Image2DMetadata{
      .storage_format = storage_format,
      .width = fbs_image->width(),
      .height = fbs_image->height(),
      .image_size = fbs_image->data()->size(),
  };
}

std::optional<AssetMetadata> SingleAssetParser::asset_metadata() const {
  switch (asset_type()) {
    case AssetType::Unknown:
      return std::nullopt;
    case AssetType::WgslSource: {
      const auto* fbs_wgsl = std::get<const IgAsset::WgslSource*>(fbs_asset_);
      if (fbs_wgsl == nullptr || fbs_wgsl->source() == nullptr) {
        return std::nullopt;
      }

      return WgslMetadata{
          .has_vertex = fbs_wgsl->vertex_entry_point() != nullptr,
          .has_fragment = fbs_wgsl->fragment_entry_point() != nullptr,
          .has_compute = fbs_wgsl->compute_entry_point() != nullptr,
          .wgsl_size = fbs_wgsl->source()->size(),
      };
    }
    case AssetType::DracoGeo: {
      const auto* fbs_draco =
          std::get<const IgAsset::DracoGeometry*>(fbs_asset_);
      if (fbs_draco == nullptr || fbs_draco->draco_bin() == nullptr) {
        return std::nullopt;
      }

      return DracoMetadata{
          .has_pos_norm = fbs_draco->pos_attrib() > -1,
          .has_texcoord = fbs_draco->normal_attrib() > -1,
          .has_bones = fbs_draco->bone_idx_attrib() > -1 &&
                       fbs_draco->bone_weight_attrib() > -1,
          .index_buffer_type =
              fbs_draco->index_format() == IgAsset::IndexFormat_Uint16
                  ? IndexBufferType::Uint16
                  : IndexBufferType::Uint32,
          .buffer_size = fbs_draco->draco_bin()->size(),
      };
    }
    case AssetType::Image2D: {
      const auto* fbs_image = std::get<const IgAsset::Image2D*>(fbs_asset_);
      return image_metadata(fbs_image);
    }
    case AssetType::Spritesheet: {
      const auto* spritesheet =
          std::get<const IgAsset::Spritesheet*>(fbs_asset_);
      if (spritesheet == nullptr || spritesheet->image() == nullptr ||
          spritesheet->sprites() == nullptr) {
        return std::nullopt;
      }

      auto image_metadata = igasset::image_metadata(spritesheet->image());
      if (!image_metadata.has_value()) {
        return std::nullopt;
      }

      return SpritesheetMetadata{
          .image_metadata = std::move(*image_metadata),
          .num_sprites = spritesheet->sprites()->size(),
      };
    }
  }

  return std::nullopt;
}

std::optional<igasset::WgslSource> SingleAssetParser::extract_wgsl_source()
    const {
  if (asset_type() != AssetType::WgslSource) {
    return std::nullopt;
  }

  const auto* fbs_wgsl = std::get<const IgAsset::WgslSource*>(fbs_asset_);

  if (fbs_wgsl == nullptr) {
    return std::nullopt;
  }

  return WgslSource::Unpack(fbs_wgsl);
}

std::optional<DracoDecoder> SingleAssetParser::extract_draco_geo() const {
  if (asset_type() != AssetType::DracoGeo) {
    return std::nullopt;
  }

  const auto* fbs_draco = std::get<const IgAsset::DracoGeometry*>(fbs_asset_);
  if (fbs_draco == nullptr) {
    return std::nullopt;
  }

  std::vector<std::string> bone_names;
  std::vector<glm::mat4> bone_inv_bind_poses;

  for (const auto* ozz_bone_name : *fbs_draco->ozz_bone_names()) {
    bone_names.push_back(ozz_bone_name->str());
  }

  for (const auto* ozz_inv_bind_pose : *fbs_draco->ozz_inv_bind_poses()) {
    glm::mat4 inv_bind_pose(1.f);

    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
        inv_bind_pose[r][c] = ozz_inv_bind_pose->values()->Get(r * 4 + c);
      }
    }

    bone_inv_bind_poses.push_back(inv_bind_pose);
  }

  auto decode_rsl = DracoDecoder::Create(
      reinterpret_cast<const char*>(fbs_draco->draco_bin()->Data()),
      fbs_draco->draco_bin()->size(), fbs_draco->pos_attrib(),
      fbs_draco->normal_attrib(),
      fbs_draco->index_format() == IgAsset::IndexFormat_Uint16
          ? IndexBufferType::Uint16
          : IndexBufferType::Uint32,
      std::move(bone_names), std::move(bone_inv_bind_poses),
      fbs_draco->tangent_attrib(), fbs_draco->bitangent_attrib(),
      fbs_draco->texcoord_attrib(), fbs_draco->bone_idx_attrib(),
      fbs_draco->bone_weight_attrib());

  if (std::holds_alternative<igasset::DracoDecoderError>(decode_rsl)) {
    ::logger()->error(
        "Draco decoding failure: {} - {}",
        igasset::to_string(
            std::get<igasset::DracoDecoderError>(decode_rsl).error_type),
        std::get<igasset::DracoDecoderError>(decode_rsl).err_message);

    return std::nullopt;
  }

  return std::get<DracoDecoder>(std::move(decode_rsl));
}

std::optional<Image2D> SingleAssetParser::extract_image2d(
    Image2DFormat format) const {
  if (asset_type() != AssetType::Image2D) {
    return std::nullopt;
  }

  const auto* fbs_image = std::get<const IgAsset::Image2D*>(fbs_asset_);
  if (fbs_image == nullptr) {
    return std::nullopt;
  }

  auto image = Image2D::Unpack(fbs_image, format);
  if (!image.has_value()) {
    ::logger()->error("Image2D failed to unpack - invalid format or data");
    return std::nullopt;
  }

  return *std::move(image);
}

std::optional<Spritesheet> SingleAssetParser::extract_spritesheet(
    Image2DFormat format) const {
  if (asset_type() != AssetType::Spritesheet) {
    return std::nullopt;
  }

  const auto* fbs_spritesheet =
      std::get<const IgAsset::Spritesheet*>(fbs_asset_);
  if (fbs_spritesheet == nullptr) {
    return std::nullopt;
  }

  auto spritesheet = Spritesheet::Unpack(fbs_spritesheet, format);
  if (!spritesheet.has_value()) {
    ::logger()->error("Spritesheet failed to unpack - invalid format or data");
    return std::nullopt;
  }

  return *std::move(spritesheet);
}

}  // namespace igasset
