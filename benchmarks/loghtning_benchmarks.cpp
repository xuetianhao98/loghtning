#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <latch>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <vector>

#include "loghtning/format.hpp"
#include "loghtning/loghtning.hpp"

namespace {

constexpr auto kIdleSleep = std::chrono::milliseconds{1};
constexpr std::size_t kQueueCapacity = 1U << 20U;
constexpr std::size_t kMaxBatchSize = 4096;
constexpr int kDrainBatchSize = 1024;
constexpr int kFileBatchSize = 256;
constexpr int kMessagesPerThread = 2048;
constexpr int kEnqueueIterations = 65536;
constexpr int kDrainIterations = 64;
constexpr int kMultithreadIterations = 16;
constexpr int kFileIterations = 16;

std::atomic<std::uint64_t> next_id{0};

[[nodiscard]] auto unique_name(std::string_view prefix) -> std::string {
  auto const id = next_id.fetch_add(1, std::memory_order_relaxed);
  return std::string{prefix} + "_" + std::to_string(id);
}

[[nodiscard]] auto benchmark_options() -> loghtning::BackendOptions {
  return {
      .idle_sleep = kIdleSleep,
      .queue_capacity = kQueueCapacity,
      .max_batch_size = kMaxBatchSize,
  };
}

[[nodiscard]] auto create_null_logger(std::string_view prefix)
    -> loghtning::LoggerPtr {
  auto sink = std::make_shared<loghtning::NullSink>();
  return std::make_shared<loghtning::Logger>(
      unique_name(std::string{prefix} + "_logger"),
      std::vector<loghtning::SinkPtr>{sink});
}

void start_backend_for_benchmark() {
  loghtning::Backend::stop();
  loghtning::Backend::start(benchmark_options());
  loghtning::Frontend::preallocate();
}

void stop_backend_for_benchmark() {
  loghtning::Backend::flush();
  loghtning::Backend::stop();
}

void record_dropped_messages(benchmark::State& state, std::uint64_t dropped) {
  state.counters["dropped"] = static_cast<double>(dropped);
  if (dropped != 0) {
    state.SkipWithError("log messages were dropped");
  }
}

void BM_RuntimeDisabledDebugCall(benchmark::State& state) {
  start_backend_for_benchmark();
  auto logger = create_null_logger("disabled_debug");
  logger->set_level(loghtning::Level::info);
  auto const dropped_before = logger->dropped_messages();

  for (auto _ : state) {
    LOGHTNING_DEBUG(logger, "debug value {}", 42);
  }

  auto const dropped = logger->dropped_messages() - dropped_before;
  stop_backend_for_benchmark();
  record_dropped_messages(state, dropped);
  state.SetItemsProcessed(state.iterations());
}

void BM_FrontendEnqueueNoArgs(benchmark::State& state) {
  start_backend_for_benchmark();
  auto logger = create_null_logger("enqueue_no_args");
  auto const dropped_before = logger->dropped_messages();

  for (auto _ : state) {
    LOGHTNING_INFO(logger, "plain message");
  }

  auto const dropped = logger->dropped_messages() - dropped_before;
  stop_backend_for_benchmark();
  record_dropped_messages(state, dropped);
  state.SetItemsProcessed(state.iterations());
}

void BM_FrontendEnqueueIntegerArgs(benchmark::State& state) {
  start_backend_for_benchmark();
  auto logger = create_null_logger("enqueue_int_args");
  auto const dropped_before = logger->dropped_messages();

  int index = 0;
  for (auto _ : state) {
    LOGHTNING_INFO(logger, "value {} other {}", index, index + 1);
    ++index;
  }

  auto const dropped = logger->dropped_messages() - dropped_before;
  stop_backend_for_benchmark();
  record_dropped_messages(state, dropped);
  state.SetItemsProcessed(state.iterations());
}

void BM_FrontendEnqueueStringArgs(benchmark::State& state) {
  start_backend_for_benchmark();
  auto logger = create_null_logger("enqueue_string_args");
  auto const dropped_before = logger->dropped_messages();
  std::string const owned = "owned payload";
  std::string_view const view = "view payload";

  int index = 0;
  for (auto _ : state) {
    LOGHTNING_INFO(logger, "owned {} view {} index {}", owned, view, index);
    ++index;
  }

  auto const dropped = logger->dropped_messages() - dropped_before;
  stop_backend_for_benchmark();
  record_dropped_messages(state, dropped);
  state.SetItemsProcessed(state.iterations());
}

void BM_FormatNoArgs(benchmark::State& state) {
  std::tuple<> args;
  for (auto _ : state) {
    auto message = loghtning::detail::format_tuple("plain message", args);
    benchmark::DoNotOptimize(message);
  }
  state.SetItemsProcessed(state.iterations());
}

void BM_FormatIntegerArgs(benchmark::State& state) {
  std::tuple<int, int, int> args{1, 2, 3};
  for (auto _ : state) {
    auto message =
        loghtning::detail::format_tuple("values {} {} {}", args);
    benchmark::DoNotOptimize(message);
  }
  state.SetItemsProcessed(state.iterations());
}

void BM_FormatStringArgs(benchmark::State& state) {
  std::tuple<std::string, std::string_view, char const*> args{
      "owned payload", "view payload", "c string payload"};
  for (auto _ : state) {
    auto message =
        loghtning::detail::format_tuple("strings {} {} {}", args);
    benchmark::DoNotOptimize(message);
  }
  state.SetItemsProcessed(state.iterations());
}

void BM_EndToEndDrainNullSink(benchmark::State& state) {
  start_backend_for_benchmark();
  auto logger = create_null_logger("drain_null");
  auto const dropped_before = logger->dropped_messages();

  std::int64_t items = 0;
  for (auto _ : state) {
    for (int index = 0; index < kDrainBatchSize; ++index) {
      LOGHTNING_INFO(logger, "drain item {} payload {}", index, "text");
    }
    loghtning::Backend::flush();
    items += kDrainBatchSize;
  }

  auto const dropped = logger->dropped_messages() - dropped_before;
  stop_backend_for_benchmark();
  record_dropped_messages(state, dropped);
  state.SetItemsProcessed(items);
}

void BM_MultithreadEndToEndNullSink(benchmark::State& state) {
  auto const thread_count = static_cast<int>(state.range(0));
  auto const total_messages = thread_count * kMessagesPerThread;

  start_backend_for_benchmark();
  auto logger = create_null_logger("multithread_null");
  auto const dropped_before = logger->dropped_messages();

  std::int64_t items = 0;
  for (auto _ : state) {
    std::latch ready{thread_count};
    std::latch start{1};
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(thread_count));

    for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
      threads.emplace_back([&, thread_index] {
        loghtning::Frontend::preallocate();
        ready.count_down();
        start.wait();

        for (int index = 0; index < kMessagesPerThread; ++index) {
          LOGHTNING_INFO(logger, "thread {} item {}", thread_index, index);
        }
      });
    }

    ready.wait();
    start.count_down();

    for (auto& thread : threads) {
      thread.join();
    }
    loghtning::Backend::flush();
    items += total_messages;
  }

  auto const dropped = logger->dropped_messages() - dropped_before;
  stop_backend_for_benchmark();
  record_dropped_messages(state, dropped);
  state.SetItemsProcessed(items);
}

void BM_FileSinkEndToEnd(benchmark::State& state) {
  auto const path = std::filesystem::current_path() /
                    (unique_name("loghtning_file_bench") + ".log");
  std::error_code remove_error;
  std::filesystem::remove(path, remove_error);

  start_backend_for_benchmark();
  auto sink = std::make_shared<loghtning::FileSink>(path);
  auto logger = std::make_shared<loghtning::Logger>(
      unique_name("file_logger"), std::vector<loghtning::SinkPtr>{sink});
  auto const dropped_before = logger->dropped_messages();

  std::int64_t items = 0;
  for (auto _ : state) {
    for (int index = 0; index < kFileBatchSize; ++index) {
      LOGHTNING_INFO(logger, "file item {} payload {}", index, "text");
    }
    loghtning::Backend::flush();
    logger->flush_sinks();
    items += kFileBatchSize;
  }

  auto const dropped = logger->dropped_messages() - dropped_before;
  loghtning::Backend::flush();
  loghtning::Backend::stop();
  logger->flush_sinks();
  logger.reset();
  sink.reset();
  remove_error.clear();
  std::filesystem::remove(path, remove_error);
  record_dropped_messages(state, dropped);
  state.SetItemsProcessed(items);
}

}  // namespace

BENCHMARK(BM_RuntimeDisabledDebugCall)->Unit(benchmark::kNanosecond);

BENCHMARK(BM_FrontendEnqueueNoArgs)
    ->Iterations(kEnqueueIterations)
    ->Unit(benchmark::kNanosecond);
BENCHMARK(BM_FrontendEnqueueIntegerArgs)
    ->Iterations(kEnqueueIterations)
    ->Unit(benchmark::kNanosecond);
BENCHMARK(BM_FrontendEnqueueStringArgs)
    ->Iterations(kEnqueueIterations)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(BM_FormatNoArgs)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_FormatIntegerArgs)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_FormatStringArgs)->Unit(benchmark::kNanosecond);

BENCHMARK(BM_EndToEndDrainNullSink)
    ->Iterations(kDrainIterations)
    ->UseRealTime()
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_MultithreadEndToEndNullSink)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Iterations(kMultithreadIterations)
    ->UseRealTime()
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_FileSinkEndToEnd)
    ->Iterations(kFileIterations)
    ->UseRealTime()
    ->Unit(benchmark::kMicrosecond);
