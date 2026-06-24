#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "loghtning/loghtning.hpp"

namespace {

class CaptureSink final : public loghtning::Sink {
 public:
  void flush() override {
    std::lock_guard lock{mutex_};
    ++flush_count_;
  }

  [[nodiscard]] auto records() const -> std::vector<loghtning::LogRecord> {
    std::lock_guard lock{mutex_};
    return records_;
  }

  [[nodiscard]] auto flush_count() const -> int {
    std::lock_guard lock{mutex_};
    return flush_count_;
  }

 private:
  void write_impl(loghtning::LogRecord const& record) override {
    std::lock_guard lock{mutex_};
    records_.push_back(record);
  }

  mutable std::mutex mutex_;
  std::vector<loghtning::LogRecord> records_;
  int flush_count_{0};
};

class LoghtningTest : public ::testing::Test {
 protected:
  void SetUp() override { loghtning::Backend::stop(); }

  void TearDown() override {
    loghtning::Backend::flush();
    loghtning::Backend::stop();
  }

  [[nodiscard]] auto unique_name(std::string_view prefix) -> std::string {
    auto const id = next_id_.fetch_add(1, std::memory_order_relaxed);
    return std::string{prefix} + "_" + std::to_string(id);
  }

  [[nodiscard]] auto unique_log_path(std::string_view stem)
      -> std::filesystem::path {
    auto path =
        std::filesystem::temp_directory_path() / (unique_name(stem) + ".log");
    std::filesystem::remove(path);
    return path;
  }

 private:
  static inline std::atomic<int> next_id_{0};
};

[[nodiscard]] auto contains_message(
    std::vector<loghtning::LogRecord> const& records, std::string_view message)
    -> bool {
  return std::ranges::any_of(records, [message](auto const& record) {
    return record.message == message;
  });
}

[[nodiscard]] auto read_file(std::filesystem::path const& path) -> std::string {
  std::ifstream file{path};
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

}  // namespace

TEST_F(LoghtningTest, SingleFrontendThreadFlushesAllMessages) {
  auto sink = std::make_shared<CaptureSink>();
  auto logger = loghtning::Frontend::create_or_get_logger(
      unique_name("single_frontend"), sink);

  loghtning::Backend::start({.queue_capacity = 512});
  loghtning::Frontend::preallocate();

  for (int i = 0; i < 200; ++i) {
    LOGHTNING_INFO(logger, "message {}", i);
  }

  loghtning::Backend::flush();

  auto const records = sink->records();
  ASSERT_EQ(records.size(), 200U);
  EXPECT_EQ(records.front().message, "message 0");
  EXPECT_EQ(records.back().message, "message 199");
}

TEST_F(LoghtningTest, MultiFrontendThreadsDrainPerThreadQueues) {
  auto sink = std::make_shared<CaptureSink>();
  auto logger = loghtning::Frontend::create_or_get_logger(
      unique_name("multi_frontend"), sink);

  loghtning::Backend::start({.queue_capacity = 256});

  std::vector<std::thread> threads;
  for (int thread_id = 0; thread_id < 4; ++thread_id) {
    threads.emplace_back([logger, thread_id] {
      loghtning::Frontend::preallocate();
      for (int index = 0; index < 50; ++index) {
        LOGHTNING_DEBUG(logger, "thread {} item {}", thread_id, index);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  loghtning::Backend::flush();

  auto const records = sink->records();
  ASSERT_EQ(records.size(), 200U);
  EXPECT_TRUE(contains_message(records, "thread 0 item 0"));
  EXPECT_TRUE(contains_message(records, "thread 3 item 49"));
}

TEST_F(LoghtningTest, MultipleSinksOnOneLoggerReceiveSameRecords) {
  auto first_sink = std::make_shared<CaptureSink>();
  auto second_sink = std::make_shared<CaptureSink>();
  auto logger = loghtning::Frontend::create_or_get_logger(
      unique_name("multi_sink"),
      std::vector<loghtning::SinkPtr>{first_sink, second_sink});

  loghtning::Backend::start({.queue_capacity = 128});

  for (int i = 0; i < 20; ++i) {
    LOGHTNING_WARNING(logger, "shared {}", i);
  }

  loghtning::Backend::flush();

  auto const first_records = first_sink->records();
  auto const second_records = second_sink->records();
  ASSERT_EQ(first_records.size(), 20U);
  ASSERT_EQ(second_records.size(), 20U);
  EXPECT_EQ(first_records[7].message, second_records[7].message);
  EXPECT_EQ(first_records[7].message, "shared 7");
}

TEST_F(LoghtningTest, SinkLevelFilterAppliesPerSink) {
  auto info_sink = std::make_shared<CaptureSink>();
  auto error_sink = std::make_shared<CaptureSink>();
  info_sink->set_level(loghtning::Level::info);
  error_sink->set_level(loghtning::Level::error);

  auto logger = loghtning::Frontend::create_or_get_logger(
      unique_name("sink_filter"),
      std::vector<loghtning::SinkPtr>{info_sink, error_sink});

  loghtning::Backend::start({.queue_capacity = 128});

  LOGHTNING_DEBUG(logger, "debug only logger accepts but sinks reject");
  LOGHTNING_INFO(logger, "visible to info sink");
  LOGHTNING_ERROR(logger, "visible to both sinks");

  loghtning::Backend::flush();

  auto const info_records = info_sink->records();
  auto const error_records = error_sink->records();
  ASSERT_EQ(info_records.size(), 2U);
  ASSERT_EQ(error_records.size(), 1U);
  EXPECT_TRUE(contains_message(info_records, "visible to info sink"));
  EXPECT_TRUE(contains_message(info_records, "visible to both sinks"));
  EXPECT_EQ(error_records.front().message, "visible to both sinks");
}

TEST_F(LoghtningTest, LoggerLevelSkipsArgumentEvaluation) {
  auto sink = std::make_shared<CaptureSink>();
  auto logger = loghtning::Frontend::create_or_get_logger(
      unique_name("logger_filter"), sink);

  loghtning::Backend::start({.queue_capacity = 64});
  logger->set_level(loghtning::Level::info);

  int evaluations = 0;
  auto expensive_arg = [&evaluations] {
    ++evaluations;
    return "expensive";
  };

  LOGHTNING_DEBUG(logger, "hidden {}", expensive_arg());
  EXPECT_EQ(evaluations, 0);

  LOGHTNING_INFO(logger, "shown {}", expensive_arg());
  EXPECT_EQ(evaluations, 1);

  loghtning::Backend::flush();
  auto const records = sink->records();
  ASSERT_EQ(records.size(), 1U);
  EXPECT_EQ(records.front().message, "shown expensive");
}

TEST_F(LoghtningTest, DeferredFormattingCopiesStringArguments) {
  auto sink = std::make_shared<CaptureSink>();
  auto logger = loghtning::Frontend::create_or_get_logger(
      unique_name("string_copy"), sink);

  loghtning::Backend::start({.queue_capacity = 64});

  std::string owned = "before";
  std::string_view view = owned;
  char const* c_string = owned.c_str();
  LOGHTNING_INFO(logger, "owned {} view {} cstr {}", owned, view, c_string);

  owned = "after";
  view = owned;

  loghtning::Backend::flush();
  auto const records = sink->records();
  ASSERT_EQ(records.size(), 1U);
  EXPECT_EQ(records.front().message, "owned before view before cstr before");
}

TEST_F(LoghtningTest, FlushWithoutAnyLogReturnsPromptly) {
  loghtning::Backend::start({.idle_sleep = std::chrono::milliseconds{20}});

  auto result =
      std::async(std::launch::async, [] { loghtning::Backend::flush(); });

  EXPECT_EQ(result.wait_for(std::chrono::seconds{1}),
            std::future_status::ready);
  result.get();
}

TEST_F(LoghtningTest, StopDrainsAcceptedMessages) {
  auto sink = std::make_shared<CaptureSink>();
  auto logger = loghtning::Frontend::create_or_get_logger(
      unique_name("stop_drains"), sink);

  loghtning::Backend::start({.queue_capacity = 512});

  for (int i = 0; i < 100; ++i) {
    LOGHTNING_INFO(logger, "before stop {}", i);
  }

  loghtning::Backend::stop();

  auto const records = sink->records();
  ASSERT_EQ(records.size(), 100U);
  EXPECT_EQ(records.front().message, "before stop 0");
  EXPECT_EQ(records.back().message, "before stop 99");
  EXPECT_FALSE(loghtning::Backend::is_running());
}

TEST_F(LoghtningTest, FileSinkWritesFormattedMessage) {
  auto path = unique_log_path("file_sink");

  loghtning::Backend::start({.queue_capacity = 64});
  auto sink = loghtning::Frontend::create_or_get_sink<loghtning::FileSink>(
      unique_name("file_sink"), path);
  auto logger = loghtning::Frontend::create_or_get_logger(
      unique_name("file_logger"), sink);

  LOGHTNING_ERROR(logger, "file value {}", 42);

  loghtning::Backend::flush();
  auto const contents = read_file(path);
  EXPECT_NE(contents.find("ERROR"), std::string::npos);
  EXPECT_NE(contents.find("file value 42"), std::string::npos);

  std::filesystem::remove(path);
}
