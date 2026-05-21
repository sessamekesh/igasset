#include <tool-utils/log-level.h>

namespace toolutils {

spdlog::level::level_enum to_spdlog_level(LogLevel level) {
  switch (level) {
    case LogLevel::Error:
      return spdlog::level::err;
    case LogLevel::Warn:
      return spdlog::level::warn;
    case LogLevel::Debug:
      return spdlog::level::debug;
    case LogLevel::Info:
      return spdlog::level::info;
    case LogLevel::Trace:
      return spdlog::level::trace;
  }

  return spdlog::level::warn;
}

}  // namespace toolutils
