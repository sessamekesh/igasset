#ifndef TOOLUTILS_STRINGIFY_H
#define TOOLUTILS_STRINGIFY_H

#include <igasset-gen/schema/igasset.h>
#include <picosha2.h>
#include <tool-utils/schema/types.h>
#include <iostream>
#include <string>

namespace toolutils {

static constexpr std::string to_string(IgAsset::SingleAssetData type) {
  switch (type) {
    case IgAsset::SingleAssetData_WgslSource:
      return "WgslSource";
    case IgAsset::SingleAssetData_DracoGeometry:
      return "DracoGeometry";
    case IgAsset::SingleAssetData_OzzSkeleton:
      return "OzzSkeleton";
    case IgAsset::SingleAssetData_OzzAnimation:
      return "OzzAnimation";
    case IgAsset::SingleAssetData_Image2D:
      return "Image2D";
    default:
      return "<< Unknown >>";
  }

  std::unreachable();
}

static constexpr std::string to_string(IgAsset::IndexFormat index_format) {
  switch (index_format) {
    case IgAsset::IndexFormat_Uint16:
      return "Uint16";
    case IgAsset::IndexFormat_Uint32:
      return "Uint32";
    default:
      return "<< Unknown >>";
  }
  std::unreachable();
}

static constexpr std::string to_string(IgAsset::Image2DEncoding encoding) {
  switch (encoding) {
    case IgAsset::Image2DEncoding_RGBA8Unorm:
      return "RGBA8Unorm";
    case IgAsset::Image2DEncoding_ETC1S:
      return "ETC1S";
    case IgAsset::Image2DEncoding_UASTC_LDR_4_4:
      return "UASTC_LDR_4_4";
    default:
      return "<< Unknown >>";
  }
  std::unreachable();
}

static void dump_wgsl_contents(const IgAsset::WgslSource* wgsl_source) {
  std::cout << "------- WgslSource Contents -------" << std::endl;
  std::cout << "Source bytelength: " << wgsl_source->source()->size()
            << std::endl;
  std::cout << "Source hash: "
            << picosha2::hash256_hex_string(wgsl_source->source()->str())
            << std::endl;
  std::cout << "Vertex entry point: "
            << wgsl_source->vertex_entry_point()->str() << std::endl;
  std::cout << "Fragment entry point: "
            << wgsl_source->fragment_entry_point()->str() << std::endl;
  std::cout << "Compute entry point: "
            << wgsl_source->compute_entry_point()->str() << std::endl;
}

static void dump_image2d_contents(const IgAsset::Image2D* image2d) {
  std::cout << "------- Image2D Contents -------" << std::endl;
  std::cout << "Encoding: " << to_string(image2d->encoding()) << std::endl;
  std::cout << "Width: " << image2d->width() << std::endl;
  std::cout << "Height: " << image2d->height() << std::endl;
  auto const* const pixels = image2d->data();
  size_t const nbytes = pixels ? pixels->size() : 0;
  std::cout << "Data bytelength: " << nbytes << std::endl;
  std::string const data_hash =
      nbytes > 0 ? picosha2::hash256_hex_string(pixels->data(),
                                                pixels->data() + nbytes)
                 : picosha2::hash256_hex_string(std::string{});
  std::cout << "Data hash: " << data_hash << std::endl;
  std::cout << "Bits per pixel: "
            << (image2d->width() && image2d->height()
                    ? nbytes * 8.f /
                          static_cast<float>(image2d->width() *
                                             image2d->height())
                    : 0.f)
            << std::endl;
}

}  // namespace toolutils

#endif
