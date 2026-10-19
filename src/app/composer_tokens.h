#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string_view>
#include <vector>

namespace app {
  struct ComposerToken {
    enum Kind { User, Role, Channel, Emoji } kind;
    std::size_t begin, end;
    std::string_view id, name;
  };
  inline std::vector<ComposerToken> composerTokens(std::string_view text) {
    std::vector<ComposerToken> result;
    std::size_t fence = 0;
    for (std::size_t at = 0; at < text.size();) {
      if (text[at] == '\\') {
        at += std::min<std::size_t>(2, text.size() - at);
        continue;
      }
      if (text[at] == '`') {
        auto end = at + 1;
        while (end < text.size() && text[end] == '`')
          ++end;
        if (!fence)
          fence = end - at;
        else if (end - at == fence)
          fence = 0;
        at = end;
        continue;
      }
      if (fence || text[at] != '<') {
        ++at;
        continue;
      }
      const auto end = text.find('>', at + 1);
      if (end == std::string_view::npos)
        break;
      auto token = text.substr(at + 1, end - at - 1);
      ComposerToken value{ComposerToken::User, at, end + 1, {}, {}};
      if (token.starts_with('@')) {
        token.remove_prefix(1);
        if (token.starts_with('&')) {
          value.kind = ComposerToken::Role;
          token.remove_prefix(1);
        } else if (token.starts_with('!'))
          token.remove_prefix(1);
      } else if (token.starts_with('#')) {
        value.kind = ComposerToken::Channel;
        token.remove_prefix(1);
      } else if (token.starts_with(':') || token.starts_with("a:")) {
        value.kind = ComposerToken::Emoji;
        token.remove_prefix(token.starts_with("a:") ? 2 : 1);
        const auto colon = token.find(':');
        if (colon == std::string_view::npos || colon == 0) {
          ++at;
          continue;
        }
        value.name = token.substr(0, colon);
        if (!std::ranges::all_of(value.name, [](unsigned char c) {
              return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            })) {
          ++at;
          continue;
        }
        token.remove_prefix(colon + 1);
      } else {
        ++at;
        continue;
      }
      std::uint64_t id = 0;
      const auto parsed = std::from_chars(token.data(), token.data() + token.size(), id);
      if (id
          && parsed.ec == std::errc{}
          && parsed.ptr == token.data() + token.size()
          && token.front() >= '0'
          && token.front() <= '9') {
        value.id = token;
        result.push_back(value);
        at = end + 1;
      } else
        ++at;
    }
    return result;
  }
} // namespace app
