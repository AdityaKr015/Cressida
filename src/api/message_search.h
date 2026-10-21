#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace api {
  struct SearchToken {
    std::size_t begin = 0, end = 0;
    std::string key, value;
  };
  struct MessageSearch {
    std::string content, canonical, error;
    std::vector<std::pair<std::string, std::string>> parameters;
    explicit operator bool() const { return error.empty() && (!content.empty() || !parameters.empty()); }
  };
  using SearchResolver = std::function<std::optional<std::string>(std::string_view key, std::string_view value)>;
  std::vector<SearchToken> messageSearchTokens(std::string_view source);
  MessageSearch parseMessageSearch(std::string_view source, const SearchResolver& resolve = {});
} // namespace api
