#ifndef IGASSET_GEN_IGASSET_GENERATOR_H
#define IGASSET_GEN_IGASSET_GENERATOR_H

#include <igasset-gen/assimp-geo-processor.h>
#include <igasset-gen/basisu-processor.h>
#include <igasset-gen/exec-config.h>
#include <igasset-gen/schema/igasset-gen-plan.h>
#include <igasset-gen/wgsl-copy-processor.h>
#include <igasync/promise.h>
#include <igasync/task_list.h>
#include <spdlog/logger.h>
#include <tool-utils/filesystem.h>

#include <memory>
#include <optional>

namespace igassetgen {

class IgassetGenerator {
 public:
  IgassetGenerator(std::shared_ptr<spdlog::logger> log,
                   PlanInvocationConfig config,
                   std::shared_ptr<igasync::TaskList> io_task_list,
                   std::shared_ptr<igasync::TaskList> exec_task_list,
                   std::shared_ptr<toolutils::Filesystem> filesystem)
      : log_(log->clone("IgassetGenerator")),
        config_(config),
        io_task_list_(io_task_list),
        exec_task_list_(exec_task_list),
        filesystem_(filesystem),
        wgsl_copy_processor_(std::nullopt),
        assimp_geo_processor_(std::nullopt) {}
  IgassetGenerator(const IgassetGenerator&) = delete;
  IgassetGenerator& operator=(const IgassetGenerator&) = delete;
  IgassetGenerator(IgassetGenerator&&) = default;
  IgassetGenerator& operator=(IgassetGenerator&&) = default;
  ~IgassetGenerator() = default;

 public:
  std::shared_ptr<igasync::Promise<bool>> generate_igasset(
      const IgAssetGen::SingleIgassetGenAction* action) const;

 protected:
  WgslCopyProcessor& wgsl_copy_processor() const;
  AssimpGeoProcessor& assimp_geo_processor() const;
  BasisuProcessor& basisu_processor() const;

 private:
  std::shared_ptr<toolutils::Filesystem> filesystem_;
  std::shared_ptr<spdlog::logger> log_;
  PlanInvocationConfig config_;
  std::shared_ptr<igasync::TaskList> io_task_list_;
  std::shared_ptr<igasync::TaskList> exec_task_list_;

 private:
  mutable std::optional<WgslCopyProcessor> wgsl_copy_processor_;
  mutable std::optional<AssimpGeoProcessor> assimp_geo_processor_;
  mutable std::optional<BasisuProcessor> basisu_processor_;
};

}  // namespace igassetgen

#endif
