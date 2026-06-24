#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "loghtning/format.hpp"
#include "loghtning/level.hpp"
#include "loghtning/metadata.hpp"
#include "loghtning/sinks.hpp"

namespace loghtning {
class Logger;
}  // namespace loghtning

namespace loghtning::detail {

class QueuedEvent;
class ThreadQueue;

auto enqueue_event(std::unique_ptr<QueuedEvent> event) -> bool;
void ensure_backend_started();

// Type-erased queued work item. The metadata pointer comes from a static object
// emitted by the logging macro, so it remains valid for the process lifetime.
class QueuedEvent {
 public:
  QueuedEvent(Logger* logger, MacroMetadata const* metadata,
              std::chrono::system_clock::time_point timestamp,
              std::string thread_id);
  virtual ~QueuedEvent() = default;

  QueuedEvent(QueuedEvent const&) = delete;
  auto operator=(QueuedEvent const&) -> QueuedEvent& = delete;

  [[nodiscard]] auto timestamp() const noexcept
      -> std::chrono::system_clock::time_point {
    return timestamp_;
  }

  void dispatch();

 protected:
  [[nodiscard]] auto format_string() const noexcept -> char const* {
    return metadata_->format;
  }

 private:
  [[nodiscard]] virtual auto make_message() const -> std::string = 0;
  Logger* logger_{nullptr};
  MacroMetadata const* metadata_{nullptr};
  std::chrono::system_clock::time_point timestamp_;
  std::string thread_id_;
};

// Typed event that owns the copied argument tuple until the backend formats it.
template <typename... Args>
class LogEvent final : public QueuedEvent {
 public:
  LogEvent(Logger* logger, MacroMetadata const* metadata,
           std::chrono::system_clock::time_point timestamp,
           std::string thread_id, Args&&... args)
      : QueuedEvent(logger, metadata, timestamp, std::move(thread_id)),
        args_(std::forward<Args>(args)...) {}

 private:
  [[nodiscard]] auto make_message() const -> std::string override {
    return format_tuple(format_string(), args_);
  }

  std::tuple<std::decay_t<Args>...> args_;
};

}  // namespace loghtning::detail

namespace loghtning {

class Logger {
 public:
  Logger(std::string name, std::vector<SinkPtr> sinks);

  Logger(Logger const&) = delete;
  auto operator=(Logger const&) -> Logger& = delete;

  [[nodiscard]] auto name() const noexcept -> std::string const& {
    return name_;
  }

  void set_level(Level level) noexcept {
    level_.store(level, std::memory_order_relaxed);
  }

  [[nodiscard]] auto level() const noexcept -> Level {
    return level_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] auto should_log(Level level) const noexcept -> bool {
    return static_cast<int>(level) >=
               static_cast<int>(level_.load(std::memory_order_relaxed)) &&
           level != Level::off;
  }

  template <typename... Args>
  auto log(MacroMetadata const* metadata, Args&&... args) -> bool {
    if ((metadata == nullptr) || !should_log(metadata->level)) {
      return false;
    }

    detail::ensure_backend_started();

    // Capture all user data before leaving the caller thread; backend
    // formatting must not depend on stack variables or temporary string views
    // still living.
    auto thread_id = current_thread_id();
    auto event = std::make_unique<detail::LogEvent<detail::StoredArg<Args>...>>(
        this, metadata, std::chrono::system_clock::now(), std::move(thread_id),
        detail::store_arg(std::forward<Args>(args))...);

    if (!detail::enqueue_event(std::move(event))) {
      dropped_messages_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }

    return true;
  }

  [[nodiscard]] auto dropped_messages() const noexcept -> std::uint64_t {
    return dropped_messages_.load(std::memory_order_relaxed);
  }

  void write_backend(MacroMetadata const& metadata,
                     std::chrono::system_clock::time_point timestamp,
                     std::string thread_id, std::string message);

  void flush_sinks();

 private:
  [[nodiscard]] static auto current_thread_id() -> std::string;

  std::string name_;
  std::vector<SinkPtr> sinks_;
  std::atomic<Level> level_{Level::trace};
  std::atomic<std::uint64_t> dropped_messages_{0};
};

using LoggerPtr = std::shared_ptr<Logger>;

}  // namespace loghtning
