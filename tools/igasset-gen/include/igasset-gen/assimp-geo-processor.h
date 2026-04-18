#ifndef IGASSET_GEN_ASSIMP_GEO_PROCESSOR_H
#define IGASSET_GEN_ASSIMP_GEO_PROCESSOR_H

#include <igasset-gen/exec-config.h>
#include <igasset-gen/filesystem.h>
#include <igasset-gen/schema/igasset-gen-plan.h>
#include <igasync/promise.h>
#include <igasync/task_list.h>
#include <spdlog/spdlog.h>

#include <memory>

namespace igassetgen {

class AssimpGeoProcessor {
 public:
  AssimpGeoProcessor(std::shared_ptr<spdlog::logger> log,
                     PlanInvocationConfig config,
                     std::shared_ptr<igasync::TaskList> io_task_list,
                     std::shared_ptr<igasync::TaskList> exec_task_list,
                     std::shared_ptr<Filesystem> filesystem)
      : log_(log->clone("AssimpGeoProcessor")),
        config_(config),
        io_task_list_(io_task_list),
        exec_task_list_(exec_task_list),
        filesystem_(filesystem) {}
  AssimpGeoProcessor(const AssimpGeoProcessor&) = delete;
  AssimpGeoProcessor& operator=(const AssimpGeoProcessor&) = delete;
  AssimpGeoProcessor(AssimpGeoProcessor&&) = default;
  AssimpGeoProcessor& operator=(AssimpGeoProcessor&&) = default;
  ~AssimpGeoProcessor() = default;

 public:
  //
  // Execute a AssimpToDracoAction action
  std::shared_ptr<igasync::Promise<bool>> draco_asset_from_assimp(
      const IgAssetGen::AssimpToDracoAction* action) const;

 private:
  std::shared_ptr<spdlog::logger> log_;
  PlanInvocationConfig config_;
  std::shared_ptr<igasync::TaskList> io_task_list_;
  std::shared_ptr<igasync::TaskList> exec_task_list_;
  std::shared_ptr<Filesystem> filesystem_;
};

}  // namespace igassetgen

#endif
