#pragma once

#include <chrono>
#include <cstddef>

namespace loghtning {

struct BackendOptions {
  std::chrono::milliseconds idle_sleep{2};
  std::size_t queue_capacity{4096};
  std::size_t max_batch_size{4096};
};

class Backend {
 public:
  static void start(BackendOptions options = {});
  static void stop();
  static void flush();
  static auto is_running() noexcept -> bool;
};

}  // namespace loghtning
