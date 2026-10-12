#ifndef IGASSET_TOOL_UTILS_FILESYSTEM_H
#define IGASSET_TOOL_UTILS_FILESYSTEM_H

#include <flatbuffers/flatbuffer_builder.h>
#include <spdlog/logger.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace toolutils {

//
// Filesystem - small synchronous wrapper around binary I/O used by the
// generator tools (igasset-gen, igpack-bundle, ...). All reads and writes
// happen inline on the calling thread (input files are always local - any
// remote storage is managed by the caller before these tools run).
//
class Filesystem {
 public:
  explicit Filesystem(std::shared_ptr<spdlog::logger> log)
      : log_(log->clone("Filesystem")) {}

  std::optional<std::string> read_bin(std::filesystem::path path) const;

  bool write_bin(std::filesystem::path path,
                 flatbuffers::FlatBufferBuilder& fbb) const;

 private:
  std::shared_ptr<spdlog::logger> log_;
};

}  // namespace toolutils

#endif
