#ifndef IGASSET_TOOL_UTILS_RUNTIME_TIMER_H
#define IGASSET_TOOL_UTILS_RUNTIME_TIMER_H

#include <spdlog/logger.h>

#include <chrono>
#include <memory>
#include <string>

namespace toolutils {

class RuntimeTimer {
 public:
  RuntimeTimer(std::shared_ptr<spdlog::logger> log, std::string task_name);
  ~RuntimeTimer();

  RuntimeTimer(const RuntimeTimer&) = delete;
  RuntimeTimer& operator=(const RuntimeTimer&) = delete;

 private:
  std::shared_ptr<spdlog::logger> log_;
  std::string task_name_;
  std::chrono::high_resolution_clock::time_point start_;
};

}  // namespace toolutils

#endif
