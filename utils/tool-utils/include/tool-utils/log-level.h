#ifndef IGASSET_TOOL_UTILS_LOG_LEVEL_H
#define IGASSET_TOOL_UTILS_LOG_LEVEL_H

#include <spdlog/spdlog.h>

namespace toolutils {

enum class LogLevel : int {
  Error,
  Warn,
  Debug,
  Info,
  Trace,
};

spdlog::level::level_enum to_spdlog_level(LogLevel level);

}  // namespace toolutils

#endif
