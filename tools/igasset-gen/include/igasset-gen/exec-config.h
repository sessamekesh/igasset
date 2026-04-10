#ifndef IGASSET_GEN_EXEC_CONFIG_H
#define IGASSET_GEN_EXEC_CONFIG_H

#include <filesystem>

namespace igassetgen {

struct PlanInvocationConfig {
  std::filesystem::file_time_type PlanFileLastModified;
  std::filesystem::path InputAssetPathRoot;
  std::filesystem::path IgassetPathRoot;
  bool CleanBuild;
};

}  // namespace igassetgen

#endif
