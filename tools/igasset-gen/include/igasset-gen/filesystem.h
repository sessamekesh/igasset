#ifndef IGASSET_GEN_FILESYSTEM_H
#define IGASSET_GEN_FILESYSTEM_H

#include <flatbuffers/flatbuffer_builder.h>
#include <igasset-gen/exec-config.h>
#include <igasync/promise.h>
#include <igasync/task_list.h>
#include <spdlog/logger.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace igassetgen {

class Filesystem : public std::enable_shared_from_this<Filesystem> {
 public:
  Filesystem(std::shared_ptr<spdlog::logger> log,
             std::shared_ptr<igasync::TaskList> io_task_list,
             PlanInvocationConfig config)
      : log_(log->clone("Filesystem")),
        io_task_list_(io_task_list),
        config_(config) {}

  std::shared_ptr<igasync::Promise<bool>> write_bin_async(
      std::filesystem::path path,
      std::shared_ptr<flatbuffers::FlatBufferBuilder> fbb) const;

  std::optional<std::string> read_bin(std::filesystem::path path) const;

  bool write_bin(std::filesystem::path path,
                 flatbuffers::FlatBufferBuilder& fbb) const;

 private:
  std::shared_ptr<spdlog::logger> log_;
  std::shared_ptr<igasync::TaskList> io_task_list_;
  PlanInvocationConfig config_;
};

}  // namespace igassetgen

#endif
