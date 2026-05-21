#include <enumerate-igpack/schema/igpack.h>
#include <picosha2.h>
#include <tool-utils/stringify.h>
#include <CLI/CLI.hpp>
#include <cstdlib>
#include <fstream>
#include <print>

using namespace toolutils;

int main(int argc, char** argv) {
  CLI::App app{
      "enumerate-igpack - a tool for inspecting the contents of an igpack "
      "bundle file",
      "enumerate-igpack"};

  std::filesystem::path igpack_path;
  app.add_option(
         "igpack", igpack_path,
         "Path to the .igpack file to read and process (local or absolute)")
      ->required(true)
      ->check(CLI::ExistingFile);

  CLI11_PARSE(app, argc, argv);

  //
  // Read and validate the igpack file
  std::ifstream fin(igpack_path, std::ios::binary);
  if (!fin) {
    std::cerr << "Failed to open " + igpack_path.string() + " for reading"
              << std::endl;
    return EXIT_FAILURE;
  }
  fin.seekg(0, std::ios::end);
  std::vector<uint8_t> data(fin.tellg());
  fin.seekg(0, std::ios::beg);
  fin.read(reinterpret_cast<char*>(data.data()), data.size());

  flatbuffers::Verifier verifier(data.data(), data.size());
  if (!IgAsset::VerifyAssetPackBuffer(verifier)) {
    std::cerr << "Failed to verify igpack file" << std::endl;
    return EXIT_FAILURE;
  }
  auto igpack = IgAsset::GetAssetPack(data.data());
  if (!igpack) {
    std::cerr << "Failed to process igpack file" << std::endl;
    return EXIT_FAILURE;
  }

  //
  // Output information about this asset bundle
  std::string sha256 = picosha2::hash256_hex_string(data);

  std::cout << "------- Asset Pack Summary -------" << std::endl;
  std::cout << "Asset Pack: " << igpack_path.string() << std::endl;
  std::cout << "SHA256: " << sha256 << std::endl;
  std::cout << "Asset Count: " << igpack->assets()->size() << std::endl;
  std::println("File size: {}", data.size());

  for (const auto* asset : *igpack->assets()) {
    std::println(" - {} ({})", asset->name()->str(),
               to_string(asset->data_type()));
  }

  std::cout << "\n---------------------\n" << std::endl;

  for (const auto* asset : *igpack->assets()) {
    std::println("------- {} -------", asset->name()->str());
    std::println("Type: {}", to_string(asset->data_type()));

    switch (asset->data_type()) {
      case IgAsset::SingleAssetData_WgslSource:
        dump_wgsl_contents(asset->data_as_WgslSource());
        break;
      case IgAsset::SingleAssetData_Image2D:
        dump_image2d_contents(asset->data_as_Image2D());
        break;
      default:
        break;
    }
  }

  return EXIT_SUCCESS;
}
