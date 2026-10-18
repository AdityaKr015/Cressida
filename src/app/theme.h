#pragma once
#include "app/syntax_highlight.h"
#include "app/theme_catalog.h"
#include "render/core/color.h"

#include <string_view>

namespace app::theme {
  inline Color background, sidebar, panel, raised, foreground, muted, accent, ink;
  inline Color railBackground, danger, destructive, success;
  inline ThemePalette palette = defaultThemePalette(false);
  inline bool lightTheme = false;
  inline void apply(const ThemePalette& colors, bool light) {
    palette = colors;
    lightTheme = light;
    background = rgbHex(colors.background);
    sidebar = rgbHex(colors.sidebar);
    panel = rgbHex(colors.panel);
    raised = rgbHex(colors.raised);
    foreground = rgbHex(colors.foreground);
    muted = rgbHex(colors.muted);
    accent = rgbHex(colors.accent);
    ink = rgbHex(colors.ink);
    railBackground = rgbHex(colors.rail);
    danger = rgbHex(colors.danger);
    destructive = rgbHex(colors.destructive);
    success = rgbHex(colors.success);
  }
  inline void select(std::string_view mode) { apply(defaultThemePalette(mode == "light"), mode == "light"); }
  inline Color syntaxColor(CodeToken token) {
    switch (token) {
    case CodeToken::Keyword:
      return rgbHex(palette.keyword);
    case CodeToken::String:
      return rgbHex(palette.string);
    case CodeToken::Comment:
      return muted;
    case CodeToken::Number:
      return rgbHex(palette.number);
    case CodeToken::Function:
      return rgbHex(palette.function);
    case CodeToken::Type:
      return rgbHex(palette.type);
    case CodeToken::Property:
      return rgbHex(palette.property);
    case CodeToken::Preprocessor:
      return rgbHex(palette.preprocessor);
    case CodeToken::Added:
      return rgbHex(palette.added);
    case CodeToken::Removed:
      return rgbHex(palette.removed);
    default:
      return foreground;
    }
  }
  inline Color presenceColor(std::string_view status) {
    return rgbHex(status == "online" ? 0x43B581 : status == "idle" ? 0xFAA61A : status == "dnd" ? 0xED4245 : 0x989BAE);
  }
  inline std::string_view presenceName(std::string_view status) {
    return status == "online"   ? "Online"
        : status == "idle"      ? "Idle"
        : status == "dnd"       ? "Do Not Disturb"
        : status == "invisible" ? "Invisible"
                                : "Connected";
  }
} // namespace app::theme
