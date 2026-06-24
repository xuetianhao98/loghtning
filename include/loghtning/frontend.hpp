#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "loghtning/backend.hpp"
#include "loghtning/logger.hpp"
#include "loghtning/sinks.hpp"

namespace loghtning {

class Frontend {
 public:
  static void preallocate();

  // Sinks are named process-wide objects so multiple loggers can share the same
  // output without constructing duplicate files or streams.
  template <typename TSink, typename... Args>
  static auto create_or_get_sink(std::string const& sink_name, Args&&... args)
      -> std::shared_ptr<TSink> {
    static_assert(std::is_base_of_v<Sink, TSink>);
    auto& registry = registries();
    std::lock_guard lock{registry.mutex};

    if (auto found = registry.sinks.find(sink_name);
        found != registry.sinks.end()) {
      return std::dynamic_pointer_cast<TSink>(found->second);
    }

    auto sink = std::make_shared<TSink>(std::forward<Args>(args)...);
    registry.sinks.emplace(sink_name, sink);
    return sink;
  }

  static auto create_or_get_logger(std::string const& logger_name, SinkPtr sink)
      -> LoggerPtr;
  static auto create_or_get_logger(std::string const& logger_name,
                                   std::vector<SinkPtr> sinks) -> LoggerPtr;
  static auto get_logger(std::string const& logger_name) -> LoggerPtr;
  static void flush_all_sinks();

 private:
  // Small global registry for the minimal build; quill's full implementation
  // has richer manager classes around the same idea.
  struct Registries {
    std::mutex mutex;
    std::unordered_map<std::string, SinkPtr> sinks;
    std::unordered_map<std::string, LoggerPtr> loggers;
  };

  static auto registries() -> Registries&;
};

auto simple_logger(std::string logger_name = "root") -> LoggerPtr;
auto file_logger(std::filesystem::path path, std::string logger_name = "root")
    -> LoggerPtr;

}  // namespace loghtning
