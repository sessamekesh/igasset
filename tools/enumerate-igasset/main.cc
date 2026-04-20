#include <enumerate-igasset/schema/igasset.h>
#include <flatbuffers/verifier.h>
#include <picosha2.h>
#include <CLI/CLI.hpp>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include "igasset/schema/types.h"


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
                          static_cast<float>(image2d->width() * image2d->height())
                    : 0.f)
            << std::endl;
}

int main(int argc, char** argv) {
  CLI::App app{
      "enumerate-igasset - a tool for inspecting the contents of an IgAsset "
      "file",
      "enumerate-igasset"};

  bool single_threaded = false;
  app.add_flag("--st", single_threaded, "If set, run single-threaded")
      ->default_val(false);

  bool dump_images = false;
  app.add_flag("--dump_images", dump_images,
               "If set, dump images to temporary directory")
      ->default_val(false);

  std::optional<std::filesystem::path> tmpdir;
  app.add_option("--tmpdir", tmpdir,
                 "Directory for intermediate files (created if missing; must "
                 "not be a regular file)");

  std::filesystem::path igasset_path;
  app.add_option("igasset", igasset_path,
                 "Path to the .igasset file to read and process (local or "
                 "absolute)")
      ->required()
      ->check(CLI::ExistingFile);

  app.parse_complete_callback([&] {
    if (!tmpdir.has_value()) {
      return;
    }
    std::filesystem::path const p = *tmpdir;
    if (std::filesystem::exists(p)) {
      if (!std::filesystem::is_directory(p)) {
        throw CLI::ValidationError(
            "--tmpdir", "path exists but is not a directory: " + p.string());
      }
    } else {
      std::error_code ec;
      std::filesystem::create_directories(p, ec);
      if (ec) {
        throw CLI::ValidationError("--tmpdir", "failed to create directory: " +
                                                   std::string(ec.message()));
      }
    }
  });

  CLI11_PARSE(app, argc, argv);

  //
  // Read and validate the igasset file
  std::ifstream fin(igasset_path, std::ios::binary);
  if (!fin) {
    std::cerr << "Failed to open " + igasset_path.string() + " for reading"
              << std::endl;
    return EXIT_FAILURE;
  }
  fin.seekg(0, std::ios::end);
  std::vector<uint8_t> data(fin.tellg());
  fin.seekg(0, std::ios::beg);
  fin.read(reinterpret_cast<char*>(data.data()), data.size());

  flatbuffers::Verifier verifier(data.data(), data.size());
  if (!IgAsset::VerifyAssetBuffer(verifier)) {
    std::cerr << "Failed to verify igasset file" << std::endl;
    return EXIT_FAILURE;
  }
  auto asset = IgAsset::GetAsset(data.data());
  if (!asset) {
    std::cerr << "Failed to get asset from igasset file" << std::endl;
    return EXIT_FAILURE;
  }

  //
  // Ouput information about this asset
  std::string sha256 = picosha2::hash256_hex_string(data);

  std::cout << "------- Asset Summary -------" << std::endl;
  std::cout << "Asset: " << igasset_path.string() << std::endl;
  std::cout << "Type: " << to_string(asset->asset_type()) << std::endl;
  std::cout << "SHA256: " << sha256 << std::endl;
  std::cout << "File size: " << data.size() << std::endl;
  std::cout << "\n";

  switch (asset->asset_type()) {
    case IgAsset::SingleAssetData_WgslSource:
      dump_wgsl_contents(asset->asset_as_WgslSource());
      break;
      case IgAsset::SingleAssetData_Image2D:
      dump_image2d_contents(asset->asset_as_Image2D());
      break;
    default:
      break;
  }

  static_cast<void>(single_threaded);
  static_cast<void>(dump_images);
  static_cast<void>(igasset_path);
  static_cast<void>(tmpdir);

  return EXIT_SUCCESS;
}
