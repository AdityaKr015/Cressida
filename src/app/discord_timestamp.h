#pragma once
#include <charconv>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace app {
  inline std::optional<std::string> discordTimestamp(std::string_view token, std::time_t now = std::time(nullptr)) {
    if (!token.starts_with("t:") || token.size() > 26)
      return {};
    token.remove_prefix(2);
    const auto separator = token.find(':');
    const auto number = token.substr(0, separator);
    std::int64_t seconds = 0;
    const auto parsed = std::from_chars(number.data(), number.data() + number.size(), seconds);
    if (parsed.ec != std::errc{}
        || parsed.ptr != number.data() + number.size()
        || number.empty()
        || seconds < -62135596800LL
        || seconds > 253402300799LL)
      return {};
    const char style = separator == std::string_view::npos ? 'f' : separator + 2 == token.size() ? token.back() : '\0';
    if (style == 'R') {
      const auto difference = seconds - static_cast<std::int64_t>(now);
      const auto distance = difference < 0 ? -difference : difference;
      std::int64_t count = distance;
      const char* unit = "second";
      for (auto [size, name] :
           {std::pair{31536000LL, "year"}, {2592000LL, "month"}, {86400LL, "day"}, {3600LL, "hour"}, {60LL, "minute"}})
        if (distance >= size) {
          count = distance / size;
          unit = name;
          break;
        }
      auto text = std::to_string(count) + " " + unit + (count == 1 ? "" : "s");
      return difference > 0 ? "in " + text : text + " ago";
    }
    const char* format = style == 't' ? "%H:%M"
        : style == 'T'                ? "%H:%M:%S"
        : style == 'd'                ? "%d/%m/%Y"
        : style == 'D'                ? "%e %B %Y"
        : style == 'f'                ? "%e %B %Y %H:%M"
        : style == 'F'                ? "%A, %e %B %Y %H:%M"
                                      : nullptr;
    if (!format)
      return {};
    const auto value = static_cast<std::time_t>(seconds);
    std::tm time{};
    if (!localtime_r(&value, &time))
      return {};
    char buffer[128];
    if (!std::strftime(buffer, sizeof(buffer), format, &time))
      return {};
    std::string result(buffer);
    if (result.starts_with(' '))
      result.erase(0, 1);
    return result;
  }
} // namespace app
