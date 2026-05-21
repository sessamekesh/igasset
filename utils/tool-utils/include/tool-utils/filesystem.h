#ifndef IGASSET_TOOL_UTILS_FILESYSTEM_H
#define IGASSET_TOOL_UTILS_FILESYSTEM_H

#include <flatbuffers/flatbuffer_builder.h>
#include <igasync/promise.h>
#include <igasync/task_list.h>
#include <spdlog/logger.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace toolutils {

//
// Filesystem - small async wrapper around binary I/O used by the generator
// tools (igasset-gen, igpack-bundle, ...). Reads happen synchronously on the
// calling thread; writes can be enqueued onto the provided I/O TaskList.
//
class Filesystem : public std::enable_shared_from_this<Filesystem> {
 public:
  Filesystem(std::shared_ptr<spdlog::logger> log,
             std::shared_ptr<igasync::TaskList> io_task_list)
      : log_(log->clone("Filesystem")),
        io_task_list_(std::move(io_task_list)) {}

  std::shared_ptr<igasync::Promise<bool>> write_bin_async(
      std::filesystem::path path,
      std::shared_ptr<flatbuffers::FlatBufferBuilder> fbb) const;

  std::optional<std::string> read_bin(std::filesystem::path path) const;

  bool write_bin(std::filesystem::path path,
                 flatbuffers::FlatBufferBuilder& fbb) const;

 private:
  std::shared_ptr<spdlog::logger> log_;
  std::shared_ptr<igasync::TaskList> io_task_list_;
};

}  // namespace toolutils

#endif
