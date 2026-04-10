#include <flatbuffers/flatbuffer_builder.h>
#include <igasset-gen/filesystem.h>
#include <igasync/promise.h>


#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace igassetgen {

std::shared_ptr<igasync::Promise<bool>> Filesystem::write_bin_async(
    std::filesystem::path path,
    std::shared_ptr<flatbuffers::FlatBufferBuilder> fbb) const {
  return io_task_list_->run(
      [log = log_, fs = shared_from_this(), fbb = std::move(fbb),
       path]() -> bool { return fs->write_bin(path, *fbb); });
}

std::optional<std::string> Filesystem::read_bin(
    std::filesystem::path path) const {
  try {
    std::string assimp_bin = "";
    auto assimp_bin_size = std::filesystem::file_size(path);
    assimp_bin.resize(assimp_bin_size);
    std::ifstream fin(path, std::ios::beg | std::ios::binary);
    if (!fin) {
      log_->error("Could not open {} for reading - aborting", path.string());
      return std::nullopt;
    }
    fin.read(&assimp_bin[0], assimp_bin_size);
    return assimp_bin;
  } catch (std::filesystem::filesystem_error e) {
    log_->error("Filesystem error: {}", e.what());
    return std::nullopt;
  } catch (std::ifstream::failure e) {
    log_->error("Filesystem error: {}", e.what());
    return std::nullopt;
  }
}

bool Filesystem::write_bin(std::filesystem::path path,
                           flatbuffers::FlatBufferBuilder& fbb) const {
  try {
    std::filesystem::path output_dir = path;
    if (output_dir.has_filename()) {
      output_dir.remove_filename();
    }
    if (!std::filesystem::exists(output_dir)) {
      std::filesystem::create_directories(output_dir);
    }

    std::ofstream fout(path, std::ios::binary);
    if (!fout) {
      log_->error("Failed to open file for writing at {}", path.string());
      return false;
    }
    if (!fout.write(reinterpret_cast<const char*>(fbb.GetBufferPointer()),
                    fbb.GetSize())) {
      log_->error("Failed to write to file {}", path.string());
      return false;
    }

    return true;
  } catch (std::filesystem::filesystem_error e) {
    log_->error("Filesystem error: {}", e.what());
    return false;
  }
}

}  // namespace igassetgen
