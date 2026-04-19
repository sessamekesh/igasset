#include "CLI/CLI.hpp"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

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
  // TODO (kamaron): Continue here! Output information about the data stored in
  //  the IgAsset file.

  static_cast<void>(single_threaded);
  static_cast<void>(dump_images);
  static_cast<void>(igasset_path);
  static_cast<void>(tmpdir);

  return EXIT_SUCCESS;
}
