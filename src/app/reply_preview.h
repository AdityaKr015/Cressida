#pragma once

#include "discord/models.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace app {
  // Reply references are one-line previews, independent of the original body
  // layout. Bound work and flatten every paragraph separator before ellipsizing.
  inline std::string replyPreview(const discord::MessageReference& reply, std::string_view author) {
    std::string result = "↳ ";
    auto append = [&](std::string_view text) {
      const auto limit = std::min(text.size(), std::size_t{4096});
      for (std::size_t i = 0; i < limit;) {
        const auto c = static_cast<unsigned char>(text[i]);
        const std::size_t length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        if (i + length > limit)
          break;
        const auto character = text.substr(i, length);
        const bool separator = c < 0x20
            || c == 0x7F
            || character == "\xC2\x85"
            || character == "\xE2\x80\xA8"
            || character == "\xE2\x80\xA9";
        if (separator) {
          if (result.back() != ' ')
            result += ' ';
        } else
          result += character;
        i += length;
      }
    };
    append(author);
    result += ": ";
    append(reply.deleted ? "Original message deleted" : reply.content.empty() ? "Original message" : reply.content);
    return result;
  }
} // namespace app
