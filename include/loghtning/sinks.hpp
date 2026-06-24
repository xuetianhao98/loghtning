#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "loghtning/level.hpp"
#include "loghtning/metadata.hpp"

namespace loghtning {

struct LogRecord {
  std::chrono::system_clock::time_point timestamp{};
  Level level{Level::info};
  std::string logger_name;
  std::string thread_id;
  SourceLocation source;
  std::string message;
};

class Sink {
 public:
  virtual ~Sink() = default;

  Sink(Sink const&) = delete;
  auto operator=(Sink const&) -> Sink& = delete;

  void set_level(Level level) noexcept { level_ = level; }

  [[nodiscard]] auto level() const noexcept -> Level { return level_; }

  void write(LogRecord const& record);
  virtual void flush() = 0;

 protected:
  Sink() = default;

 private:
  virtual void write_impl(LogRecord const& record) = 0;

  Level level_{Level::trace};
};

class PatternFormatter {
 public:
  [[nodiscard]] auto format(LogRecord const& record) const -> std::string;
};

class OstreamSink final : public Sink {
 public:
  explicit OstreamSink(std::ostream& output);

  void flush() override;

 private:
  void write_impl(LogRecord const& record) override;

  std::ostream* output_{nullptr};
  PatternFormatter formatter_{};
  std::mutex mutex_;
};

class ConsoleSink final : public Sink {
 public:
  enum class Stream { stdout_stream, stderr_stream };

  explicit ConsoleSink(Stream stream = Stream::stdout_stream);

  void flush() override;

 private:
  void write_impl(LogRecord const& record) override;

  Stream stream_{Stream::stdout_stream};
  PatternFormatter formatter_{};
  std::mutex mutex_;
};

class FileSink final : public Sink {
 public:
  explicit FileSink(std::filesystem::path path);

  void flush() override;

 private:
  void write_impl(LogRecord const& record) override;

  std::ofstream file_;
  PatternFormatter formatter_{};
  std::mutex mutex_;
};

class NullSink final : public Sink {
 public:
  void flush() override {}

 private:
  void write_impl(LogRecord const&) override {}
};

using SinkPtr = std::shared_ptr<Sink>;

}  // namespace loghtning
