#include "app/font_catalog.h"

#include <fontconfig/fontconfig.h>
#include <memory>

namespace app {
  namespace {
    std::string searchKey(const std::string& value) {
      std::unique_ptr<FcChar8, decltype(&FcStrFree)> lower(
          FcStrDowncase(reinterpret_cast<const FcChar8*>(value.c_str())), FcStrFree
      );
      return lower ? reinterpret_cast<const char*>(lower.get()) : value;
    }
  } // namespace
  std::vector<std::string> installedFontFamilies() {
    std::vector<std::string> result;
    if (!FcInit())
      return result;
    // Refresh only when the user opens the picker, never during repainting.
    FcInitBringUptoDate();
    std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)> pattern(FcPatternCreate(), FcPatternDestroy);
    std::unique_ptr<FcObjectSet, decltype(&FcObjectSetDestroy)> fields(
        FcObjectSetBuild(FC_FAMILY, nullptr), FcObjectSetDestroy
    );
    if (!pattern || !fields)
      return result;
    std::unique_ptr<FcFontSet, decltype(&FcFontSetDestroy)> fonts(
        FcFontList(nullptr, pattern.get(), fields.get()), FcFontSetDestroy
    );
    if (!fonts)
      return result;
    for (int i = 0; i < fonts->nfont; ++i) {
      FcChar8* family = nullptr;
      if (FcPatternGetString(fonts->fonts[i], FC_FAMILY, 0, &family) == FcResultMatch && family) {
        std::string name(reinterpret_cast<const char*>(family));
        if (!name.empty() && validFontFamily(name))
          result.push_back(std::move(name));
      }
    }
    std::ranges::sort(result, [](const auto& a, const auto& b) {
      const auto left = searchKey(a), right = searchKey(b);
      return left == right ? a < b : left < right;
    });
    const auto duplicate =
        std::ranges::unique(result, [](const auto& a, const auto& b) { return searchKey(a) == searchKey(b); });
    result.erase(duplicate.begin(), duplicate.end());
    return result;
  }
  bool fontFamilyMatches(const std::string& family, const std::string& query) {
    return query.empty() || searchKey(family).contains(searchKey(query));
  }
} // namespace app
