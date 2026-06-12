#ifndef IGASSET_SINGLE_ASSET_PARSER_H
#define IGASSET_SINGLE_ASSET_PARSER_H

#include <igasset/asset_metadata.h>
#include <igasset/draco_dec.h>
#include <igasset/image2d.h>
#include <igasset/schema/igasset.h>
#include <igasset/schema/igpack.h>

#include <igasset/spritesheet.h>
#include <igasset/wgsl_source.h>
#include <cstddef>
#include <optional>
#include <variant>

namespace igasset {

enum class AssetType {
  Unknown,
  WgslSource,
  DracoGeo,
  Image2D,
  Spritesheet,
};

/**
 * NON-OWNING wrapper around flatbuffer IgAsset::SingleAsset* reference. Use to
 *  extract information about single assets, and parse them into individual
 *  logical (C++ types) asset types.
 * Serves as a facade between Flatbuffer SingleAsset type and igasset types.
 */
class SingleAssetParser {
 public:
  using FbsAssetT =
      std::variant<std::nullptr_t, const IgAsset::WgslSource*,
                   const IgAsset::DracoGeometry*, const IgAsset::Image2D*,
                   const IgAsset::Spritesheet*>;

 public:
  explicit SingleAssetParser(
      const IgAsset::SingleAsset* fbs_asset);                   // igpack ctor
  explicit SingleAssetParser(const IgAsset::Asset* fbs_asset);  // igasset ctor
  ~SingleAssetParser();

  AssetType asset_type() const;
  std::optional<std::vector<Image2DFormat>> compatible_image_formats() const;
  std::optional<AssetMetadata> asset_metadata() const;

  std::optional<igasset::WgslSource> extract_wgsl_source() const;
  std::optional<DracoDecoder> extract_draco_geo() const;
  std::optional<Image2D> extract_image2d(Image2DFormat format) const;
  std::optional<Spritesheet> extract_spritesheet(Image2DFormat format) const;

 private:
  FbsAssetT fbs_asset_;
};

}  // namespace igasset

#endif
