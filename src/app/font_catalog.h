#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace app {
  // Empty selects the system default. Family names are data, never markup.
  inline bool validFontFamily(std::string_view family) {
    return family.size() <= 512 && std::ranges::none_of(family, [](unsigned char c) { return c < 0x20 || c == 0x7f; });
  }
  std::vector<std::string> installedFontFamilies();
  bool fontFamilyMatches(const std::string& family, const std::string& query);
} // namespace app
