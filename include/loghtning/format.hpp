#pragma once

#include <concepts>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace loghtning::detail {

template <typename T>
concept StreamInsertable = requires(std::ostream& os, T const& value) {
  { os << value } -> std::same_as<std::ostream&>;
};

template <typename T>
struct IsStringLike
    : std::bool_constant<std::is_convertible_v<T, std::string_view> ||
                         std::is_convertible_v<T, char const*>> {};

template <typename T>
inline constexpr bool is_string_like_v =
    IsStringLike<std::remove_cvref_t<T>>::value;

// Formatting is deferred to the backend, so pointer/view-like string inputs
// must be copied while the caller's objects are still alive.
template <typename T>
using StoredArg =
    std::conditional_t<is_string_like_v<T>, std::string, std::decay_t<T>>;

template <typename T>
[[nodiscard]] auto store_arg(T&& value) -> StoredArg<T> {
  if constexpr (is_string_like_v<T>) {
    if constexpr (std::is_convertible_v<T, std::string_view>) {
      return std::string{std::string_view{std::forward<T>(value)}};
    } else {
      char const* ptr = static_cast<char const*>(value);
      return ptr == nullptr ? std::string{"(null)"} : std::string{ptr};
    }
  } else {
    return std::forward<T>(value);
  }
}

[[nodiscard]] inline auto stringify(std::string const& value) -> std::string {
  return value;
}

[[nodiscard]] inline auto stringify(std::string_view value) -> std::string {
  return std::string{value};
}

[[nodiscard]] inline auto stringify(char const* value) -> std::string {
  return value == nullptr ? std::string{"(null)"} : std::string{value};
}

[[nodiscard]] inline auto stringify(char* value) -> std::string {
  return value == nullptr ? std::string{"(null)"} : std::string{value};
}

[[nodiscard]] inline auto stringify(bool value) -> std::string {
  return value ? "true" : "false";
}

template <typename T>
[[nodiscard]] auto stringify(T const& value) -> std::string {
  if constexpr (StreamInsertable<T>) {
    std::ostringstream out;
    out << value;
    return out.str();
  } else {
    return "<unprintable>";
  }
}

// Minimal `{}` formatter used by the backend. It supports positional `{}` and
// escaped braces, intentionally leaving rich formatting to larger libraries.
[[nodiscard]] inline auto format_with_strings(
    std::string_view fmt, std::vector<std::string> const& args) -> std::string {
  std::string output;
  output.reserve(fmt.size() + args.size() * 8);

  std::size_t arg_index = 0;
  for (std::size_t i = 0; i < fmt.size(); ++i) {
    if (fmt[i] == '{') {
      if ((i + 1 < fmt.size()) && fmt[i + 1] == '{') {
        output.push_back('{');
        ++i;
        continue;
      }

      auto const close = fmt.find('}', i + 1);
      if (close != std::string_view::npos) {
        if (arg_index < args.size()) {
          output += args[arg_index++];
        } else {
          output.append(fmt.substr(i, close - i + 1));
        }
        i = close;
        continue;
      }
    }

    if (fmt[i] == '}' && (i + 1 < fmt.size()) && fmt[i + 1] == '}') {
      output.push_back('}');
      ++i;
      continue;
    }

    output.push_back(fmt[i]);
  }

  for (; arg_index < args.size(); ++arg_index) {
    output += " [extra:";
    output += args[arg_index];
    output += ']';
  }

  return output;
}

template <typename Tuple, std::size_t... Indexes>
[[nodiscard]] auto format_tuple_impl(std::string_view fmt, Tuple const& values,
                                     std::index_sequence<Indexes...>)
    -> std::string {
  std::vector<std::string> args;
  args.reserve(sizeof...(Indexes));
  (args.push_back(stringify(std::get<Indexes>(values))), ...);
  return format_with_strings(fmt, args);
}

template <typename... Args>
[[nodiscard]] auto format_tuple(std::string_view fmt,
                                std::tuple<Args...> const& values)
    -> std::string {
  return format_tuple_impl(fmt, values, std::index_sequence_for<Args...>{});
}

}  // namespace loghtning::detail
