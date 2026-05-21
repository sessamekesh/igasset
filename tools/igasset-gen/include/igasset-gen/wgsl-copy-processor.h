#ifndef IGASSET_GEN_WGSL_COPY_PROCESSOR_H
#define IGASSET_GEN_WGSL_COPY_PROCESSOR_H

#include <igasset-gen/exec-config.h>
#include <igasset-gen/schema/igasset-gen-plan.h>
#include <igasync/promise.h>
#include <igasync/task_list.h>
#include <spdlog/logger.h>
#include <tool-utils/filesystem.h>

#include <memory>

namespace igassetgen {

class WgslCopyProcessor {
 public:
  WgslCopyProcessor(std::shared_ptr<spdlog::logger> log,
                    PlanInvocationConfig config,
                    std::shared_ptr<igasync::TaskList> io_task_list,
                    std::shared_ptr<igasync::TaskList> exec_task_list,
                    std::shared_ptr<toolutils::Filesystem> filesystem)
      : log_(log->clone("WgslCopyProcessor")),
        config_(config),
        io_task_list_(io_task_list),
        exec_task_list_(exec_task_list),
        filesystem_(filesystem) {}
  WgslCopyProcessor(const WgslCopyProcessor&) = delete;
  WgslCopyProcessor& operator=(const WgslCopyProcessor&) = delete;
  WgslCopyProcessor(WgslCopyProcessor&&) = default;
  WgslCopyProcessor& operator=(WgslCopyProcessor&&) = default;
  ~WgslCopyProcessor() = default;

 public:
  //
  // Execute a CopyWgslSourceAction action
  //
  // @param action:
  // - input_file_path: Path of raw WGSL source from which to copy
  // - output_file_path: Path of output "engine-ready" WGSL source to write WGSL
  // IgAsset
  // - vertex_entry_point / fragment_entry_point / compute_entry_point are
  //   all passed directly to generated IgAsset
  //
  // Returns a promise which resolves to "true" if the WGSL copy action
  // succeeded, "false" otherwise. On success, a new file will be written to
  // "output_file_path" of type IgAsset::WgslSource
  std::shared_ptr<igasync::Promise<bool>> generate_wgsl_igasset(
      const IgAssetGen::CopyWgslSourceAction* action) const;

 private:
  std::shared_ptr<spdlog::logger> log_;
  PlanInvocationConfig config_;
  std::shared_ptr<igasync::TaskList> io_task_list_;
  std::shared_ptr<igasync::TaskList> exec_task_list_;
  std::shared_ptr<toolutils::Filesystem> filesystem_;
};

}  // namespace igassetgen

#endif
