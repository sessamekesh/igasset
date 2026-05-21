#include <tool-utils/runtime-timer.h>

#include <utility>

namespace toolutils {

RuntimeTimer::RuntimeTimer(std::shared_ptr<spdlog::logger> log,
                           std::string task_name)
    : log_(std::move(log)),
      task_name_(std::move(task_name)),
      start_(std::chrono::high_resolution_clock::now()) {}

RuntimeTimer::~RuntimeTimer() {
  auto end = std::chrono::high_resolution_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start_);

  log_->info("Executed {} in {}ms", task_name_,
             static_cast<float>(elapsed.count()) / 1000.f);
}

}  // namespace toolutils
