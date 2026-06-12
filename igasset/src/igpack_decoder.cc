#include <flatbuffers/verifier.h>
#include <igasset/asset_metadata.h>
#include <igasset/draco_dec.h>
#include <igasset/igpack_decoder.h>
#include <igasset/image2d.h>
#include <igasset/schema/igasset.h>
#include <igasset/single_asset_parser.h>
#include <igasset/spritesheet.h>
#include <igasset/vertex_types.h>
#include <igasset/wgsl_source.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <format>
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

// NOTICE - this logger will grab and clone the logger when first called, and
//  not respect updates to the default logger. For now this is fine, but may not
//  be expected for all users. I don't feel terribly strongly about this,
//  considering this method only ever gets called in error cases.
auto logger() {
  static auto log = spdlog::default_logger()->clone("IgpackDecoder");
  return log;
}

template <typename T>
std::variant<T, igasset::IgpackExtractError> _extract_rsl(
    std::optional<T> extract_rsl) {
  if (!extract_rsl.has_value()) {
    return igasset::IgpackExtractError::AssetExtractError;
  }
  return *std::move(extract_rsl);
}

}  // namespace

namespace igasset {

std::optional<std::shared_ptr<IgpackDecoder>> IgpackDecoder::Create(
    std::string data) {
  const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(data.data());
  auto verifier = flatbuffers::Verifier(data_ptr, data.size(), ::get_opts());
  if (!IgAsset::VerifyAssetPackBuffer(verifier)) {
    ::logger()->error("Asset pack failed flatbuffer verification");
    return std::nullopt;
  }

  return std::shared_ptr<IgpackDecoder>(new IgpackDecoder(std::move(data)));
}

std::map<std::string, AssetMetadata> IgpackDecoder::get_asset_metadata() const {
  std::map<std::string, AssetMetadata> result{};

  for (const auto* asset : *asset_pack_->assets()) {
    std::string name = asset->name()->str();
    auto metadata_opt = SingleAssetParser(asset).asset_metadata();
    if (!metadata_opt.has_value()) {
      continue;
    }

    int rename_attempt = 1;
    while (result.contains(name)) {
      name = std::format("{} (!! DUP {} !!)", asset->name()->str(),
                         rename_attempt++);
    }

    result[name] = std::move(*metadata_opt);
  }

  return result;
}

std::variant<igasset::WgslSource, IgpackExtractError>
IgpackDecoder::extract_wgsl_shader(const std::string& asset_name) const {
  for (const auto* asset : *asset_pack_->assets()) {
    if (asset->name()->str() != asset_name) {
      continue;
    }

    if (asset->data_type() != IgAsset::SingleAssetData_WgslSource) {
      return IgpackExtractError::WrongResourceType;
    }

    return ::_extract_rsl(SingleAssetParser(asset).extract_wgsl_source());
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

    return ::_extract_rsl(SingleAssetParser(asset).extract_draco_geo());
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

    return ::_extract_rsl(SingleAssetParser(asset).extract_image2d(format));
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

    return ::_extract_rsl(SingleAssetParser(asset).extract_spritesheet(format));
  }

  return IgpackExtractError::ResourceNotFound;
}

}  // namespace igasset
