#pragma once

#include "discord/models.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace app {
  inline std::string_view boundedReplyText(std::string_view text) {
    auto size = std::min(text.size(), std::size_t{4096});
    while (size > 0 && size < text.size() && (static_cast<unsigned char>(text[size]) & 0xC0) == 0x80)
      --size;
    return text.substr(0, size);
  }
  inline std::string oneLineReplyText(std::string_view text) {
    text = boundedReplyText(text);
    std::string result;
    for (std::size_t i = 0; i < text.size();) {
      const auto c = static_cast<unsigned char>(text[i]);
      const std::size_t length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
      if (i + length > text.size())
        break;
      const auto character = text.substr(i, length);
      const bool separator = c < 0x20
          || c == 0x7F
          || character == "\xC2\x85"
          || character == "\xE2\x80\xA8"
          || character == "\xE2\x80\xA9";
      if (separator) {
        if (result.empty() || result.back() != ' ')
          result += ' ';
      } else
        result += character;
      i += length;
    }
    return result;
  }
  inline std::string_view replyContent(const discord::MessageReference& reply) {
    return reply.deleted        ? "Original message deleted"
        : reply.content.empty() ? "Original message"
                                : boundedReplyText(reply.content);
  }
  // Plain text counterpart for callers/tests; rendered previews use the bounded
  // single-line runs from layoutReplyPreview, including custom emoji images.
  inline std::string replyPreview(const discord::MessageReference& reply, std::string_view author) {
    return "↳ " + oneLineReplyText(author) + ": " + oneLineReplyText(replyContent(reply));
  }
} // namespace app
