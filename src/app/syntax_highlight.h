#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace app {
  enum class CodeToken {
    Plain,
    Keyword,
    String,
    Comment,
    Number,
    Function,
    Type,
    Property,
    Preprocessor,
    Added,
    Removed
  };
  struct CodeSpan {
    std::size_t offset, length;
    CodeToken token = CodeToken::Plain;
  };
  // Byte ranges into the original source. Unknown/plain languages stay uncolored.
  std::vector<CodeSpan> highlightCode(std::string_view source, std::string_view language);
} // namespace app
