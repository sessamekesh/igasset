#include <igasset-gen/inc-build.h>
#include <spdlog/logger.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace igassetgen {

bool skip_rebuild(PlanInvocationConfig plan_config,
                  std::vector<std::filesystem::path> input_files,
                  std::filesystem::path output_file,
                  std::shared_ptr<spdlog::logger> log) {
  if (plan_config.CleanBuild) {
    log->trace(
        "Re-generating output file {} regardless of cache state because "
        "--clean was set",
        output_file.string());
    return false;
  }

  try {
    if (!std::filesystem::exists(output_file)) {
      log->trace("Output file {} does not exist yet, cannot skip rebuild",
                 output_file.string());
      return false;
    }

    auto out_last_write_time = std::filesystem::last_write_time(output_file);

    if (plan_config.PlanFileLastModified > out_last_write_time) {
      log->trace(
          "Plan changed since output {} was last generated, cannot skip "
          "rebuild",
          output_file.string());
      return false;
    }

    for (const auto& input_file : input_files) {
      if (!std::filesystem::exists(input_file)) {
        log->trace(
            "Input file {} does not exist, cannot skip rebuild (let calling "
            "process handle failure)",
            input_file.string());
        return false;
      }
      auto in_last_write_time = std::filesystem::last_write_time(input_file);
      if (in_last_write_time > out_last_write_time) {
        log->trace("Input file {} modified - re-generating output file {}",
                   input_file.string(), output_file.string());
        return false;
      }
    }

    log->trace("Using cached output file {}", output_file.string());
    return true;
  } catch (std::filesystem::filesystem_error e) {
    log->error(
        "Filesystem failure in skip_rebuild: {} - assuming output is dirty",
        e.what());
    return false;
  }
}

}  // namespace igassetgen
