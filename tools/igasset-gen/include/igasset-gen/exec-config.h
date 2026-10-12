#ifndef IGASSET_GEN_EXEC_CONFIG_H
#define IGASSET_GEN_EXEC_CONFIG_H

#include <staged-asset-io/staged-filesystem.h>

#include <filesystem>
#include <memory>

namespace igassetgen {

struct PlanInvocationConfig {
  std::filesystem::file_time_type PlanFileLastModified;
  std::shared_ptr<stagedio::StagedFilesystem> StagedFs;
  bool CleanBuild;
};

}  // namespace igassetgen

#endif