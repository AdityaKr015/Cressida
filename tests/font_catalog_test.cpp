#include "app/font_catalog.h"

#include <iostream>
#include <set>
#include <stdexcept>

int main() {
  try {
    const auto require = [](bool ok, const char* message) {
      if (!ok)
        throw std::runtime_error(message);
    };
    require(app::fontFamilyMatches("École Mono", "éCOLE"), "Font search handles Unicode case-insensitively");
    require(app::fontFamilyMatches("Noto Sans Mono", "sans mono"), "Font search matches partial family names");
    require(app::fontFamilyMatches("Any Font", ""), "An empty search shows every installed font");
    require(!app::fontFamilyMatches("Noto Sans", "serif"), "Unrelated font families are filtered out");
    require(app::validFontFamily({}) && app::validFontFamily("École Mono"), "System default and Unicode are valid");
    require(!app::validFontFamily(std::string("Bad\0Font", 8)), "Font names cannot contain null bytes");
    const auto fonts = app::installedFontFamilies();
    std::set<std::string> unique;
    for (const auto& font : fonts) {
      require(!font.empty() && app::validFontFamily(font), "The installed catalog contains usable names");
      require(unique.insert(font).second, "Font styles do not duplicate family choices");
      require(app::fontFamilyMatches(font, font), "Every installed family can be found by name");
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
