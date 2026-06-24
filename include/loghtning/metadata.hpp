#pragma once

#include <cstdint>
#include <string_view>

#include "loghtning/level.hpp"

namespace loghtning {

struct SourceLocation {
  char const* file{""};
  char const* function{""};
  std::uint_least32_t line{0};
};

struct MacroMetadata {
  char const* format{""};
  SourceLocation source{};
  Level level{Level::info};

  constexpr MacroMetadata(char const* fmt, char const* file,
                          char const* function, std::uint_least32_t line_number,
                          Level log_level) noexcept
      : format(fmt), source{file, function, line_number}, level(log_level) {}

  [[nodiscard]] constexpr auto file_name() const noexcept -> std::string_view {
    std::string_view path{source.file};
    auto pos = path.find_last_of("/\\");
    return pos == std::string_view::npos ? path : path.substr(pos + 1);
  }
};

}  // namespace loghtning
