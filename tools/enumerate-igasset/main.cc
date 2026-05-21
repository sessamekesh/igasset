#include <enumerate-igasset/schema/igasset.h>
#include <flatbuffers/verifier.h>
#include <igasset/schema/types.h>
#include <picosha2.h>
#include <tool-utils/stringify.h>
#include <CLI/CLI.hpp>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace toolutils;

int main(int argc, char** argv) {
  CLI::App app{
      "enumerate-igasset - a tool for inspecting the contents of an IgAsset "
      "file",
      "enumerate-igasset"};

  std::filesystem::path igasset_path;
  app.add_option("igasset", igasset_path,
                 "Path to the .igasset file to read and process (local or "
                 "absolute)")
      ->required()
      ->check(CLI::ExistingFile);

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

  return EXIT_SUCCESS;
}
