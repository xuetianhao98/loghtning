# loghtning

`loghtning` is a tiny C++23 logging library written as a minimal, clean-room rewrite inspired by
the core architecture of [odygrd/quill](https://github.com/odygrd/quill).

It keeps the central idea and leaves the production extras behind:

- frontend log calls capture static macro metadata and copy arguments into a typed event;
- each calling thread owns a bounded single-producer/single-consumer queue;
- one backend worker drains all frontend queues, orders a batch by timestamp, formats records, and writes sinks;
- sinks are pluggable, with console and file sinks included.

This is intentionally small. It does not attempt to provide quill's binary argument codec, metrics,
rdtsc clock, crash handling, backtrace buffers, structured JSON, file rotation, filters, or queue tuning.

## Quick Start

```cpp
#include "loghtning/loghtning.hpp"

int main() {
  auto logger = loghtning::simple_logger();

  LOGHTNING_INFO(logger, "hello from {}", "loghtning");
  LOGHTNING_WARNING(logger, "answer = {}", 42);

  loghtning::Backend::flush();
  loghtning::Backend::stop();
}
```

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Benchmarks

Benchmarks use Google Benchmark and are built from the normal `build` directory,
so IDE tooling can keep using the same `compile_commands.json`:

```sh
scripts/run_benchmarks.sh
```

The script defaults to 1 repetition. Pass a number or `--repetitions` to
override it, followed by any Google Benchmark arguments:

```sh
scripts/run_benchmarks.sh 10 --benchmark_filter=BM_Format
scripts/run_benchmarks.sh --repetitions 3 --benchmark_min_time=0.2s
```
