#pragma once

#include "ui/style.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace app {
  enum class WindowButton { Minimize, Maximize, Close };
  enum class WindowIcon { Minimize, Maximize, Restore, Close };
  inline constexpr std::array<std::string_view, 4> windowIconNames{"Minimize", "Maximize", "Restore", "Close"};
  inline constexpr std::size_t maximumWindowIconPathBytes = 4096;
  inline constexpr int windowButtonOrderCount = 6;
  enum class WindowIconStyle { Standard, Circular, Dots, Custom };
  inline constexpr int windowIconStyleCount = static_cast<int>(WindowIconStyle::Custom) + 1;
  inline bool validWindowIconPath(const std::string& path) {
    return path.size() <= maximumWindowIconPathBytes
        && path.find('\0') == std::string::npos
        && (path.empty() || path.front() == '/');
  }
  struct WindowControlPreferences {
    bool minimize = true, maximize = true, close = true, left = false, hoverHighlight = true;
    int order = 0, style = 0;
    int buttonWidth = static_cast<int>(Style::windowButtonWidth);
    std::array<std::string, windowIconNames.size()> icons;
    bool operator==(const WindowControlPreferences&) const = default;
  };
  // Before xdg-shell v5 there is no capability advertisement. Preserve the
  // traditional available actions rather than guessing from desktop names.
  class WindowCapabilities {
  public:
    static constexpr std::uint32_t protocolVersion = 5;
    static constexpr std::uint32_t maximizeCapability = 2, minimizeCapability = 4;
    void configure(std::span<const std::uint32_t> values) {
      m_pendingMinimize = std::ranges::find(values, minimizeCapability) != values.end();
      m_pendingMaximize = std::ranges::find(values, maximizeCapability) != values.end();
    }
    void surfaceConfigured() {
      m_minimize = m_pendingMinimize;
      m_maximize = m_pendingMaximize;
    }
    bool minimize() const { return m_minimize; }
    bool maximize() const { return m_maximize; }

  private:
    bool m_pendingMinimize = true, m_pendingMaximize = true, m_minimize = true, m_maximize = true;
  };
  inline std::vector<WindowButton>
  visibleWindowButtons(const WindowControlPreferences& preference, const WindowCapabilities& capabilities) {
    using B = WindowButton;
    constexpr std::array<std::array<B, 3>, windowButtonOrderCount> orders{
        {{B::Minimize, B::Maximize, B::Close},
         {B::Close, B::Minimize, B::Maximize},
         {B::Close, B::Maximize, B::Minimize},
         {B::Minimize, B::Close, B::Maximize},
         {B::Maximize, B::Minimize, B::Close},
         {B::Maximize, B::Close, B::Minimize}}
    };
    std::vector<B> result;
    for (const auto button : orders[std::clamp(preference.order, 0, windowButtonOrderCount - 1)])
      if ((button == B::Minimize && preference.minimize && capabilities.minimize())
          || (button == B::Maximize && preference.maximize && capabilities.maximize())
          || (button == B::Close && preference.close))
        result.push_back(button);
    return result;
  }
  inline WindowIcon windowButtonIcon(WindowButton button, bool maximized) {
    if (button == WindowButton::Minimize)
      return WindowIcon::Minimize;
    if (button == WindowButton::Maximize)
      return maximized ? WindowIcon::Restore : WindowIcon::Maximize;
    return WindowIcon::Close;
  }
  inline std::string_view windowIconGlyph(WindowIcon icon, int style) {
    constexpr std::array<std::string_view, windowIconNames.size()> standard{"minus", "square", "squares", "x"};
    constexpr std::array<std::string_view, windowIconNames.size()> circular{
        "circle-minus", "circle-plus", "circles", "circle-x"
    };
    return (style == static_cast<int>(WindowIconStyle::Circular) ? circular : standard)[static_cast<std::size_t>(icon)];
  }
} // namespace app
