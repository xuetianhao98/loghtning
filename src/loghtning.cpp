#include "loghtning/loghtning.hpp"

#include <algorithm>
#include <condition_variable>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "loghtning/spsc_queue.hpp"

namespace loghtning {

namespace {

auto format_timestamp(std::chrono::system_clock::time_point timestamp)
    -> std::string {
  auto const time = std::chrono::system_clock::to_time_t(timestamp);

  std::tm local{};
  // Use the thread-safe localtime variants because sinks can be flushed or
  // tested from threads other than the backend.
#if defined(_WIN32)
  localtime_s(&local, &time);
#else
  localtime_r(&time, &local);
#endif

  auto const micros = std::chrono::duration_cast<std::chrono::microseconds>(
                          timestamp.time_since_epoch())
                          .count() %
                      1'000'000;

  std::ostringstream out;
  out << std::put_time(&local, "%H:%M:%S") << '.' << std::setw(6)
      << std::setfill('0') << micros;
  return out.str();
}

}  // namespace

void Sink::write(LogRecord const& record) {
  if (static_cast<int>(record.level) < static_cast<int>(level_)) {
    return;
  }

  write_impl(record);
}

auto PatternFormatter::format(LogRecord const& record) const -> std::string {
  std::ostringstream out;
  out << format_timestamp(record.timestamp) << " [" << record.thread_id << "] "
      << std::left << std::setw(8) << to_string(record.level) << ' '
      << record.logger_name << ' ' << record.source.file << ':'
      << record.source.line << " " << record.message;
  return out.str();
}

OstreamSink::OstreamSink(std::ostream& output) : output_(&output) {}

void OstreamSink::write_impl(LogRecord const& record) {
  std::lock_guard lock{mutex_};
  (*output_) << formatter_.format(record) << '\n';
}

void OstreamSink::flush() {
  std::lock_guard lock{mutex_};
  output_->flush();
}

ConsoleSink::ConsoleSink(Stream stream) : stream_(stream) {}

void ConsoleSink::write_impl(LogRecord const& record) {
  std::lock_guard lock{mutex_};
  auto& output = stream_ == Stream::stdout_stream ? std::cout : std::cerr;
  output << formatter_.format(record) << '\n';
}

void ConsoleSink::flush() {
  std::lock_guard lock{mutex_};
  auto& output = stream_ == Stream::stdout_stream ? std::cout : std::cerr;
  output.flush();
}

FileSink::FileSink(std::filesystem::path path)
    : file_(path, std::ios::out | std::ios::app) {
  if (!file_) {
    throw std::runtime_error{"loghtning: failed to open log file " +
                             path.string()};
  }
}

void FileSink::write_impl(LogRecord const& record) {
  std::lock_guard lock{mutex_};
  file_ << formatter_.format(record) << '\n';
}

void FileSink::flush() {
  std::lock_guard lock{mutex_};
  file_.flush();
}

Logger::Logger(std::string name, std::vector<SinkPtr> sinks)
    : name_(std::move(name)), sinks_(std::move(sinks)) {
  if (sinks_.empty()) {
    throw std::invalid_argument{"loghtning: logger requires at least one sink"};
  }
}

auto Logger::current_thread_id() -> std::string {
  std::ostringstream out;
  out << std::this_thread::get_id();
  return out.str();
}

void Logger::write_backend(MacroMetadata const& metadata,
                           std::chrono::system_clock::time_point timestamp,
                           std::string thread_id, std::string message) {
  LogRecord record;
  record.timestamp = timestamp;
  record.level = metadata.level;
  record.logger_name = name_;
  record.thread_id = std::move(thread_id);
  record.source = metadata.source;
  record.message = std::move(message);

  for (auto const& sink : sinks_) {
    sink->write(record);
  }
}

void Logger::flush_sinks() {
  for (auto const& sink : sinks_) {
    sink->flush();
  }
}

namespace detail {

class ThreadQueue {
 public:
  explicit ThreadQueue(std::size_t capacity) : queue_(capacity) {}

  [[nodiscard]] auto try_push(std::unique_ptr<QueuedEvent> event) -> bool {
    return queue_.try_push(std::move(event));
  }

  [[nodiscard]] auto try_pop(std::unique_ptr<QueuedEvent>& event) -> bool {
    return queue_.try_pop(event);
  }

  [[nodiscard]] auto empty() const noexcept -> bool { return queue_.empty(); }

 private:
  SpscQueue<std::unique_ptr<QueuedEvent>> queue_;
};

// Owns the backend worker and the set of frontend queues registered by caller
// threads. This is intentionally compact compared with quill's manager layer.
class Runtime {
 public:
  void start(BackendOptions options) {
    std::lock_guard lock{lifecycle_mutex_};
    if (running_.load(std::memory_order_acquire)) {
      return;
    }

    options_ = options;
    stop_requested_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { worker_loop(); });
  }

  void stop() {
    {
      std::lock_guard lock{lifecycle_mutex_};
      if (!running_.load(std::memory_order_acquire)) {
        return;
      }
      stop_requested_.store(true, std::memory_order_release);
    }

    wake();

    if (worker_.joinable()) {
      worker_.join();
    }

    running_.store(false, std::memory_order_release);
  }

  void flush() {
    // Wait for every event submitted before this call to be processed, then
    // flush sinks. Events submitted later may be handled by a later flush.
    auto const target = submitted_.load(std::memory_order_acquire);
    wake();

    std::unique_lock lock{cv_mutex_};
    cv_.wait(lock, [this, target] {
      return processed_.load(std::memory_order_acquire) >= target ||
             !running_.load(std::memory_order_acquire);
    });

    Frontend::flush_all_sinks();
  }

  [[nodiscard]] auto is_running() const noexcept -> bool {
    return running_.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto enqueue(std::unique_ptr<QueuedEvent> event) -> bool {
    auto queue = local_queue();
    if (!queue->try_push(std::move(event))) {
      return false;
    }

    submitted_.fetch_add(1, std::memory_order_release);
    wake();
    return true;
  }

  void preallocate() { (void)local_queue(); }

 private:
  [[nodiscard]] auto local_queue() -> std::shared_ptr<ThreadQueue> {
    // Each frontend thread registers exactly one queue on first use.
    thread_local std::shared_ptr<ThreadQueue> queue = [this] {
      auto created = std::make_shared<ThreadQueue>(options_.queue_capacity);
      std::lock_guard lock{queues_mutex_};
      queues_.push_back(created);
      return created;
    }();

    return queue;
  }

  [[nodiscard]] auto snapshot_queues()
      -> std::vector<std::shared_ptr<ThreadQueue>> {
    // Drain outside the registry lock so producer threads only block while a
    // short shared_ptr snapshot is being made.
    std::lock_guard lock{queues_mutex_};
    return queues_;
  }

  [[nodiscard]] auto drain_once() -> bool {
    std::vector<std::unique_ptr<QueuedEvent>> batch;
    batch.reserve(options_.max_batch_size);

    for (auto const& queue : snapshot_queues()) {
      std::unique_ptr<QueuedEvent> event;
      while (batch.size() < options_.max_batch_size && queue->try_pop(event)) {
        batch.push_back(std::move(event));
      }
    }

    if (batch.empty()) {
      return false;
    }

    // Events arrive from independent per-thread queues; sorting each batch
    // gives a stable, timestamp-oriented merge without a global producer lock.
    std::ranges::sort(batch, {}, [](std::unique_ptr<QueuedEvent> const& event) {
      return event->timestamp();
    });

    for (auto& event : batch) {
      event->dispatch();
      processed_.fetch_add(1, std::memory_order_release);
    }

    cv_.notify_all();
    return true;
  }

  [[nodiscard]] auto has_pending_events() -> bool {
    for (auto const& queue : snapshot_queues()) {
      if (!queue->empty()) {
        return true;
      }
    }

    return processed_.load(std::memory_order_acquire) <
           submitted_.load(std::memory_order_acquire);
  }

  void worker_loop() {
    // Stop requests ask the worker to finish, but it still drains queued events
    // before leaving so normal shutdown does not lose already accepted logs.
    while (!stop_requested_.load(std::memory_order_acquire) ||
           has_pending_events()) {
      if (drain_once()) {
        continue;
      }

      std::unique_lock lock{cv_mutex_};
      cv_.wait_for(lock, options_.idle_sleep);
    }

    Frontend::flush_all_sinks();
    cv_.notify_all();
  }

  void wake() { cv_.notify_one(); }

  BackendOptions options_{};
  std::mutex lifecycle_mutex_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  std::thread worker_;

  std::mutex queues_mutex_;
  std::vector<std::shared_ptr<ThreadQueue>> queues_;

  std::mutex cv_mutex_;
  std::condition_variable cv_;
  std::atomic<std::uint64_t> submitted_{0};
  std::atomic<std::uint64_t> processed_{0};
};

auto runtime() -> Runtime& {
  static Runtime instance;
  return instance;
}

QueuedEvent::QueuedEvent(Logger* logger, MacroMetadata const* metadata,
                         std::chrono::system_clock::time_point timestamp,
                         std::string thread_id)
    : logger_(logger),
      metadata_(metadata),
      timestamp_(timestamp),
      thread_id_(std::move(thread_id)) {}

void QueuedEvent::dispatch() {
  if ((logger_ == nullptr) || (metadata_ == nullptr)) {
    return;
  }

  logger_->write_backend(*metadata_, timestamp_, std::move(thread_id_),
                         make_message());
}

auto enqueue_event(std::unique_ptr<QueuedEvent> event) -> bool {
  return runtime().enqueue(std::move(event));
}

void ensure_backend_started() {
  if (!runtime().is_running()) {
    runtime().start({});
  }
}

}  // namespace detail

void Backend::start(BackendOptions options) {
  detail::runtime().start(options);
}

void Backend::stop() { detail::runtime().stop(); }

void Backend::flush() { detail::runtime().flush(); }

auto Backend::is_running() noexcept -> bool {
  return detail::runtime().is_running();
}

void Frontend::preallocate() { detail::runtime().preallocate(); }

auto Frontend::registries() -> Registries& {
  static Registries instance;
  return instance;
}

auto Frontend::create_or_get_logger(std::string const& logger_name,
                                    SinkPtr sink) -> LoggerPtr {
  std::vector<SinkPtr> sinks;
  sinks.push_back(std::move(sink));
  return create_or_get_logger(logger_name, std::move(sinks));
}

auto Frontend::create_or_get_logger(std::string const& logger_name,
                                    std::vector<SinkPtr> sinks) -> LoggerPtr {
  auto& registry = registries();
  std::lock_guard lock{registry.mutex};

  if (auto found = registry.loggers.find(logger_name);
      found != registry.loggers.end()) {
    return found->second;
  }

  auto logger = std::make_shared<Logger>(logger_name, std::move(sinks));
  registry.loggers.emplace(logger_name, logger);
  return logger;
}

auto Frontend::get_logger(std::string const& logger_name) -> LoggerPtr {
  auto& registry = registries();
  std::lock_guard lock{registry.mutex};

  auto found = registry.loggers.find(logger_name);
  return found == registry.loggers.end() ? nullptr : found->second;
}

void Frontend::flush_all_sinks() {
  std::vector<LoggerPtr> loggers;
  {
    auto& registry = registries();
    std::lock_guard lock{registry.mutex};
    loggers.reserve(registry.loggers.size());

    for (auto const& [_, logger] : registry.loggers) {
      loggers.push_back(logger);
    }
  }

  for (auto const& logger : loggers) {
    logger->flush_sinks();
  }
}

auto simple_logger(std::string logger_name) -> LoggerPtr {
  Backend::start();
  auto sink = Frontend::create_or_get_sink<ConsoleSink>("console");
  return Frontend::create_or_get_logger(logger_name, sink);
}

auto file_logger(std::filesystem::path path, std::string logger_name)
    -> LoggerPtr {
  Backend::start();
  auto sink_name = "file:" + path.string();
  auto sink =
      Frontend::create_or_get_sink<FileSink>(sink_name, std::move(path));
  return Frontend::create_or_get_logger(logger_name, sink);
}

}  // namespace loghtning
