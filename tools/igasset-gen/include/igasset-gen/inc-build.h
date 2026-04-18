#ifndef IGASSET_GEN_INC_BUILD_H
#define IGASSET_GEN_INC_BUILD_H

#include <igasset-gen/exec-config.h>
#include <spdlog/logger.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace igassetgen {

bool skip_rebuild(PlanInvocationConfig plan_config,
                  std::vector<std::filesystem::path> input_files,
                  std::filesystem::path output_file,
                  std::shared_ptr<spdlog::logger> log);

}

#endif
