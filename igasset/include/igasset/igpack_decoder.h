#ifndef IGASSET_IGPACK_DECODER_H
#define IGASSET_IGPACK_DECODER_H

#include <igasset/draco_dec.h>
#include <igasset/image2d.h>
#include <igasset/schema/igasset.h>
#include <igasset/schema/igpack.h>
#include <igasset/spritesheet.h>
#include <igasset/wgsl_source.h>

#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace igasset {

enum class IgpackExtractError {
  ResourceNotFound,
  WrongResourceType,
  AssetExtractError,
};

constexpr const char* to_string(IgpackExtractError err) {
  switch (err) {
    case IgpackExtractError::ResourceNotFound:
      return "ResourceNotFound";
    case IgpackExtractError::WrongResourceType:
      return "WrongResourceType";
    case IgpackExtractError::AssetExtractError:
      return "AssetExtractError";
    default:
      return "<< Unknown Error >>";
  }
}

class IgpackDecoder {
 public:
  static std::shared_ptr<IgpackDecoder> Create(std::string data);

  IgpackDecoder(const IgpackDecoder&) = delete;
  IgpackDecoder& operator=(const IgpackDecoder&) = delete;

  IgpackDecoder(IgpackDecoder&& o) : raw_data_(""), asset_pack_(nullptr) {
    raw_data_ = std::move(o.raw_data_);
    asset_pack_ = IgAsset::GetAssetPack(raw_data_.data());

    o.raw_data_ = "";
    o.asset_pack_ = nullptr;
  }
  IgpackDecoder& operator=(IgpackDecoder&& o) {
    raw_data_ = std::move(o.raw_data_);
    asset_pack_ = IgAsset::GetAssetPack(raw_data_.data());

    o.raw_data_ = "";
    o.asset_pack_ = nullptr;

    return *this;
  }
  ~IgpackDecoder() = default;

  //
  // WGSL Shaders
 public:
  std::variant<igasset::WgslSource, IgpackExtractError> extract_wgl_shader(
      const std::string& asset_name) const;

  //
  // DRACO Geometry
 public:
  std::variant<DracoDecoder, IgpackExtractError> extract_draco_decoder(
      const std::string& asset_name) const;

  //
  // Textures (2D)
 public:
  std::variant<Image2D, IgpackExtractError> extract_image2d(
      const std::string& asset_name, Image2DFormat format) const;

  //
  // Spritesheets
 public:
  std::variant<Spritesheet, IgpackExtractError> extract_spritesheet(
      const std::string& asset_name, Image2DFormat format) const;

 private:
  IgpackDecoder(std::string raw_data)
      : raw_data_(std::move(raw_data)),
        asset_pack_(IgAsset::GetAssetPack(raw_data_.data())) {}

  std::string raw_data_;
  const IgAsset::AssetPack* asset_pack_;
};

}  // namespace igasset

#endif
