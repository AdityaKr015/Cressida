#pragma once
#include "render/core/color.h"

#include <string_view>

namespace app::theme {
  inline Color background, sidebar, panel, raised, foreground, muted, accent, ink;
  inline void select(std::string_view name) {
    const bool light = name == "light", midnight = name == "midnight";
    background = rgbHex(light ? 0xF5F5F7 : midnight ? 0x08090B : 0x17181D);
    sidebar = rgbHex(light ? 0xE8E9EE : midnight ? 0x101114 : 0x1E2027);
    panel = rgbHex(light ? 0xDCDEE6 : midnight ? 0x17191F : 0x24262F);
    raised = rgbHex(light ? 0xCDD0DC : 0x2E313D);
    foreground = rgbHex(light ? 0x20222C : 0xEEEFF5);
    muted = rgbHex(light ? 0x616678 : 0x989BAE);
    accent = rgbHex(light ? 0x6D4DB5 : 0xC6B5F5);
    ink = rgbHex(light ? 0xFFFFFF : 0x211A36);
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
