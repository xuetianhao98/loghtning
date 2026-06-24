#pragma once

#include <string_view>

namespace loghtning {

enum class Level : int {
  trace = 0,
  debug = 1,
  info = 2,
  warning = 3,
  error = 4,
  critical = 5,
  off = 6
};

constexpr auto to_string(Level level) noexcept -> std::string_view {
  switch (level) {
    case Level::trace:
      return "TRACE";
    case Level::debug:
      return "DEBUG";
    case Level::info:
      return "INFO";
    case Level::warning:
      return "WARNING";
    case Level::error:
      return "ERROR";
    case Level::critical:
      return "CRITICAL";
    case Level::off:
      return "OFF";
  }

  return "UNKNOWN";
}

constexpr auto to_short_string(Level level) noexcept -> std::string_view {
  switch (level) {
    case Level::trace:
      return "TRC";
    case Level::debug:
      return "DBG";
    case Level::info:
      return "INF";
    case Level::warning:
      return "WRN";
    case Level::error:
      return "ERR";
    case Level::critical:
      return "CRT";
    case Level::off:
      return "OFF";
  }

  return "UNK";
}

}  // namespace loghtning
