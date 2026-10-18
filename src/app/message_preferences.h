#pragma once

#include <array>
#include <string>
#include <string_view>

namespace app {
  // Convert standalone typed emoticons without changing code, links or escaped text.
  inline std::string convertMessageEmoticons(std::string_view text) {
    static constexpr std::pair<std::string_view, std::string_view> replacements[] = {
        {":-)", "🙂"}, {":)", "🙂"},  {":-D", "😃"}, {":D", "😃"},  {";-)", "😉"}, {";)", "😉"},
        {":-(", "🙁"}, {":(", "🙁"},  {":-P", "😛"}, {":P", "😛"},  {":p", "😛"},  {"<3", "❤️"},
        {":'(", "😢"}, {":-o", "😮"}, {":o", "😮"},  {":-O", "😮"}, {":O", "😮"}
    };
    const auto boundary = [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; };
    std::string result;
    std::size_t code = 0;
    for (std::size_t i = 0; i < text.size();) {
      if (text[i] == '\\' && i + 1 < text.size()) {
        result.append(text.substr(i, 2));
        i += 2;
        continue;
      }
      if (text[i] == '`') {
        std::size_t count = 1;
        while (i + count < text.size() && text[i + count] == '`')
          ++count;
        if (!code)
          code = count;
        else if (code == count)
          code = 0;
        result.append(text.substr(i, count));
        i += count;
        continue;
      }
      bool converted = false;
      if (!code && (!i || boundary(text[i - 1]))) {
        for (const auto& [from, to] : replacements) {
          if (text.substr(i).starts_with(from) && (i + from.size() == text.size() || boundary(text[i + from.size()]))) {
            result += to;
            i += from.size();
            converted = true;
            break;
          }
        }
      }
      if (!converted)
        result += text[i++];
    }
    return result;
  }
  // Conservatively hide paired spoiler markers in desktop previews, even when
  // malformed backticks would make a lightweight code parser ambiguous.
  inline std::string notificationMessagePreview(std::string_view text) {
    std::string output;
    for (std::size_t at = 0; at < text.size();) {
      if (text[at] == '\\' && at + 1 < text.size()) {
        output.append(text.substr(at, 2));
        at += 2;
        continue;
      }
      if (text.substr(at).starts_with("||")) {
        auto end = at + 2;
        while (end < text.size()) {
          if (text[end] == '\\' && end + 1 < text.size())
            end += 2;
          else if (text.substr(end).starts_with("||"))
            break;
          else
            ++end;
        }
        if (end < text.size()) {
          output += "[Spoiler]";
          at = end + 2;
          continue;
        }
      }
      output += text[at++];
    }
    return output;
  }
} // namespace app
