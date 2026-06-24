# Quill Core Design Extract

This note summarizes the small subset of quill's design that `loghtning` keeps.
The source reference is [odygrd/quill](https://github.com/odygrd/quill), especially its public
`Backend`, `Frontend`, `Logger`, `LogMacros`, queue, formatter, and sink layers.

## Kept

1. Hot/cold split
   - Frontend caller threads do minimal work.
   - A backend thread owns formatting and I/O.

2. Static call-site metadata
   - Log macros create one static metadata object per call site.
   - Metadata stores format string, level, file, function, and line.

3. Per-thread queue
   - Each frontend thread gets a thread-local SPSC queue.
   - The backend drains all registered queues.

4. Deferred formatting
   - Arguments are copied into a typed event on the caller thread.
   - The backend formats the message later.

5. Logger to sinks fan-out
   - A logger carries a name, level, and one or more sinks.
   - Console and file sinks implement the minimal sink contract.

## Simplified

- Quill binary-serializes argument packs; `loghtning` stores a small type-erased event with a tuple.
- Quill has bounded/unbounded queue modes; `loghtning` ships one bounded dropping SPSC queue.
- Quill has advanced timestamp and rdtsc support; `loghtning` uses `std::chrono::system_clock`.
- Quill supports pattern customization, metrics, filters, backtrace, crash handling, JSON, and rotation;
  `loghtning` intentionally omits those features.
- Quill uses `{fmt}`; `loghtning` includes a tiny `{}` formatter backed by `operator<<`.

## Flow

```mermaid
flowchart LR
  A["LOGHTNING_INFO macro"] --> B["static MacroMetadata"]
  B --> C["Logger::log"]
  C --> D["thread-local SPSC queue"]
  D --> E["Backend worker"]
  E --> F["timestamp batch ordering"]
  F --> G["format message"]
  G --> H["Logger fan-out"]
  H --> I["ConsoleSink / FileSink"]
```
