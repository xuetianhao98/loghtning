#pragma once

#include "loghtning/level.hpp"
#include "loghtning/metadata.hpp"

#define LOGHTNING_LEVEL_TRACE 0
#define LOGHTNING_LEVEL_DEBUG 1
#define LOGHTNING_LEVEL_INFO 2
#define LOGHTNING_LEVEL_WARNING 3
#define LOGHTNING_LEVEL_ERROR 4
#define LOGHTNING_LEVEL_CRITICAL 5

#ifndef LOGHTNING_ACTIVE_LEVEL
#define LOGHTNING_ACTIVE_LEVEL LOGHTNING_LEVEL_TRACE
#endif

// LOGHTNING_ACTIVE_LEVEL lets the preprocessor remove lower-severity call sites
// before they generate metadata or branches in the compiled program.
#if defined(__GNUC__) || defined(__clang__)
#define LOGHTNING_LIKELY(x) (__builtin_expect(!!(x), 1))
#else
#define LOGHTNING_LIKELY(x) (x)
#endif

// One static metadata object is emitted per call site; the hot path only passes
// its pointer plus copied arguments to the logger.
#define LOGHTNING_LOGGER_CALL(logger, level_value, fmt, ...)                   \
  do {                                                                         \
    auto&& loghtning_logger_ = (logger);                                       \
    if (loghtning_logger_ &&                                                   \
        LOGHTNING_LIKELY(loghtning_logger_->should_log(level_value))) {        \
      static constexpr ::loghtning::MacroMetadata loghtning_metadata_{         \
          fmt, __FILE__, __func__, __LINE__, level_value};                     \
      loghtning_logger_->log(&loghtning_metadata_ __VA_OPT__(, ) __VA_ARGS__); \
    }                                                                          \
  } while (false)

#if LOGHTNING_ACTIVE_LEVEL <= LOGHTNING_LEVEL_TRACE
#define LOGHTNING_TRACE(logger, fmt, ...)                  \
  LOGHTNING_LOGGER_CALL(logger, ::loghtning::Level::trace, \
                        fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOGHTNING_TRACE(logger, fmt, ...) (void)0
#endif

#if LOGHTNING_ACTIVE_LEVEL <= LOGHTNING_LEVEL_DEBUG
#define LOGHTNING_DEBUG(logger, fmt, ...)                  \
  LOGHTNING_LOGGER_CALL(logger, ::loghtning::Level::debug, \
                        fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOGHTNING_DEBUG(logger, fmt, ...) (void)0
#endif

#if LOGHTNING_ACTIVE_LEVEL <= LOGHTNING_LEVEL_INFO
#define LOGHTNING_INFO(logger, fmt, ...)                  \
  LOGHTNING_LOGGER_CALL(logger, ::loghtning::Level::info, \
                        fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOGHTNING_INFO(logger, fmt, ...) (void)0
#endif

#if LOGHTNING_ACTIVE_LEVEL <= LOGHTNING_LEVEL_WARNING
#define LOGHTNING_WARNING(logger, fmt, ...)                  \
  LOGHTNING_LOGGER_CALL(logger, ::loghtning::Level::warning, \
                        fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOGHTNING_WARNING(logger, fmt, ...) (void)0
#endif

#if LOGHTNING_ACTIVE_LEVEL <= LOGHTNING_LEVEL_ERROR
#define LOGHTNING_ERROR(logger, fmt, ...)                  \
  LOGHTNING_LOGGER_CALL(logger, ::loghtning::Level::error, \
                        fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOGHTNING_ERROR(logger, fmt, ...) (void)0
#endif

#if LOGHTNING_ACTIVE_LEVEL <= LOGHTNING_LEVEL_CRITICAL
#define LOGHTNING_CRITICAL(logger, fmt, ...)                  \
  LOGHTNING_LOGGER_CALL(logger, ::loghtning::Level::critical, \
                        fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOGHTNING_CRITICAL(logger, fmt, ...) (void)0
#endif

#define LOGHTNING_LOG_TRACE(logger, fmt, ...) \
  LOGHTNING_TRACE(logger, fmt __VA_OPT__(, ) __VA_ARGS__)
#define LOGHTNING_LOG_DEBUG(logger, fmt, ...) \
  LOGHTNING_DEBUG(logger, fmt __VA_OPT__(, ) __VA_ARGS__)
#define LOGHTNING_LOG_INFO(logger, fmt, ...) \
  LOGHTNING_INFO(logger, fmt __VA_OPT__(, ) __VA_ARGS__)
#define LOGHTNING_LOG_WARNING(logger, fmt, ...) \
  LOGHTNING_WARNING(logger, fmt __VA_OPT__(, ) __VA_ARGS__)
#define LOGHTNING_LOG_ERROR(logger, fmt, ...) \
  LOGHTNING_ERROR(logger, fmt __VA_OPT__(, ) __VA_ARGS__)
#define LOGHTNING_LOG_CRITICAL(logger, fmt, ...) \
  LOGHTNING_CRITICAL(logger, fmt __VA_OPT__(, ) __VA_ARGS__)
