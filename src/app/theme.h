#pragma once
#include "app/syntax_highlight.h"
#include "render/core/color.h"

#include <string_view>

namespace app::theme {
  inline Color background, sidebar, panel, raised, foreground, muted, accent, ink;
  inline bool lightTheme = false;
  inline void select(std::string_view name) {
    const bool light = name == "light", midnight = name == "midnight";
    lightTheme = light;
    background = rgbHex(light ? 0xF5F5F7 : midnight ? 0x08090B : 0x17181D);
    sidebar = rgbHex(light ? 0xE8E9EE : midnight ? 0x101114 : 0x1E2027);
    panel = rgbHex(light ? 0xDCDEE6 : midnight ? 0x17191F : 0x24262F);
    raised = rgbHex(light ? 0xCDD0DC : 0x2E313D);
    foreground = rgbHex(light ? 0x20222C : 0xEEEFF5);
    muted = rgbHex(light ? 0x616678 : 0x989BAE);
    accent = rgbHex(light ? 0x6D4DB5 : 0xC6B5F5);
    ink = rgbHex(light ? 0xFFFFFF : 0x211A36);
  }
  inline Color syntaxColor(CodeToken token) {
    switch (token) {
    case CodeToken::Keyword:
      return rgbHex(lightTheme ? 0x7C3DA5 : 0xC792EA);
    case CodeToken::String:
      return rgbHex(lightTheme ? 0x326B39 : 0x9BCF9F);
    case CodeToken::Comment:
      return muted;
    case CodeToken::Number:
      return rgbHex(lightTheme ? 0x9A520C : 0xF0B981);
    case CodeToken::Function:
      return rgbHex(lightTheme ? 0x2159A8 : 0x82AAFF);
    case CodeToken::Type:
      return rgbHex(lightTheme ? 0x7A5400 : 0xE6C07B);
    case CodeToken::Property:
      return rgbHex(lightTheme ? 0x086B75 : 0x82C9CF);
    case CodeToken::Preprocessor:
      return rgbHex(lightTheme ? 0x086B75 : 0x82C9CF);
    case CodeToken::Added:
      return rgbHex(lightTheme ? 0x2C723E : 0x8BD49C);
    case CodeToken::Removed:
      return rgbHex(lightTheme ? 0xAC2E40 : 0xF08B96);
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
