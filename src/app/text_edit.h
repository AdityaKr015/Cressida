#pragma once
#include <algorithm>
#include <pango/pango.h>
#include <string>
#include <string_view>
#include <vector>

namespace app {
  // UTF-8 byte positions always land on Pango grapheme boundaries.
  struct TextEdit {
    std::size_t caret = 0, anchor = 0;
    void clamp(const std::string& text) {
      caret = std::min(caret, text.size());
      anchor = std::min(anchor, text.size());
    }
    void end(const std::string& text) { caret = anchor = text.size(); }
    bool selected() const { return caret != anchor; }
    std::string selection(const std::string& text) const {
      return text.substr(std::min(caret, anchor), std::max(caret, anchor) - std::min(caret, anchor));
    }
    static std::vector<std::size_t> stops(std::string_view text) {
      const auto count = g_utf8_strlen(text.data(), text.size());
      std::vector<PangoLogAttr> attrs(count + 1);
      pango_get_log_attrs(text.data(), text.size(), -1, pango_language_get_default(), attrs.data(), attrs.size());
      std::vector<std::size_t> result{0};
      const char* at = text.data();
      for (glong i = 1; i <= count; ++i) {
        at = g_utf8_next_char(at);
        if (attrs[i].is_cursor_position)
          result.push_back(at - text.data());
      }
      return result;
    }
    void move(const std::string& text, bool right, bool extend) {
      clamp(text);
      if (selected() && !extend)
        caret = right ? std::max(caret, anchor) : std::min(caret, anchor);
      else {
        const auto positions = stops(text);
        if (right) {
          auto at = std::upper_bound(positions.begin(), positions.end(), caret);
          if (at != positions.end())
            caret = *at;
        } else {
          auto at = std::lower_bound(positions.begin(), positions.end(), caret);
          if (at != positions.begin())
            caret = *std::prev(at);
        }
      }
      if (!extend)
        anchor = caret;
    }
    void replace(std::string& text, std::string_view value) {
      clamp(text);
      const auto start = std::min(caret, anchor), length = std::max(caret, anchor) - start;
      text.replace(start, length, value);
      caret = anchor = start + value.size();
    }
    void erase(std::string& text, bool forward) {
      if (!selected())
        move(text, forward, true);
      replace(text, {});
    }
  };
} // namespace app
