#ifndef IGPACK_BUNDLE_EXEC_CONFIG_H
#define IGPACK_BUNDLE_EXEC_CONFIG_H

#include <filesystem>

namespace igpackbundle {

struct PlanInvocationConfig {
  std::filesystem::file_time_type PlanFileLastModified;
  std::filesystem::path IgassetPathRoot;
  std::filesystem::path IgpackPathRoot;
  bool CleanBuild;
};

}  // namespace igpackbundle

#endif
