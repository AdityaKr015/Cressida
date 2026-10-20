#include "app/client_application.h"
#include "app/theme.h"
#include "cursor-shape-v1-client-protocol.h"
#include "render/core/texture_manager.h"
#include "ui/controls/select.h"
#include "ui/controls/toggle.h"

#include <iostream>
#include <limits>
#include <linux/input-event-codes.h>
#include <stdexcept>

namespace app {
  void ClientApplication::demoWindowControlsStep() {
    const auto require = [](bool ok, const char* reason) {
      if (!ok)
        throw std::runtime_error(reason);
    };
    const auto widget = [&](const std::string& id) -> Node& {
      const auto at = m_widgets.find(id);
      require(at != m_widgets.end() && at->second->parent(), "Titlebar setting is a mounted native control");
      return *at->second;
    };
    const auto click = [&](const std::string& id) {
      auto& node = widget(id);
      float x = 0, y = 0;
      Node::mapToScene(&node, node.width() / 2, node.height() / 2, x, y);
      m_window.motion(x, y);
      m_window.click(x, y, BTN_LEFT);
      m_window.released();
    };
    const auto choose = [&](const std::string& id, std::size_t value) {
      static_cast<Select&>(widget("user-account-select:" + id)).setSelectedIndex(value);
    };
    const auto titleHits = [&] {
      std::vector<Hit> result;
      for (const auto& hit : m_hits)
        if (hit.action == Action::Minimize || hit.action == Action::Maximize || hit.action == Action::Close)
          result.push_back(hit);
      return result;
    };
    const auto step = m_demoInteractionStep++;
    std::clog << "Offline window controls frame " << step << '\n';
    switch (step) {
    case 0:
      require(!m_preferences.desktopDecorations && !m_profileDirty, "Titlebar replay starts with Cressida decorations");
      click("user-account-toggle:window-minimize");
      require(!m_preferences.windowControls.minimize, "Native minimize visibility toggle changes its preference");
      break;
    case 1:
      require(
          std::ranges::none_of(titleHits(), [](const auto& hit) { return hit.action == Action::Minimize; }),
          "Hidden minimize has no painted/clickable titlebar button"
      );
      windowControlPreference("window-left");
      choose("window-order", 2);
      choose("window-style", 1);
      break;
    case 2: {
      const auto hits = titleHits();
      require(
          !hits.empty() && hits.front().x == 0 && hits.front().action == Action::Close,
          "Left placement uses the selected close-first button ordering"
      );
      require(
          !m_window.titlebarDragContains(hits.front().x + hits.front().width / 2, hits.front().height / 2)
              && m_window.titlebarDragContains(m_window.width() / 2, layout::Titlebar::height / 2),
          "Window uses the rendered button layout for dragging"
      );
      m_window.motion(hits.front().x + hits.front().width / 2, hits.front().height / 2);
      require(
          m_window.cursorShape() == WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
          "Relocated window controls retain the native pointer cursor"
      );
      choose("window-style", 2);
      windowControlPreference("window-maximize");
      windowControlPreference("window-close");
      break;
    }
    case 3:
      require(
          titleHits().empty() && m_window.titlebarDragContains(Style::spaceXs, Style::spaceXs),
          "Hiding every button releases its drag area without losing the titlebar"
      );
      windowControlPreference("window-reset");
      choose("window-style", 3);
      m_settingsStatus.clear();
      break;
    case 4: {
      require(m_inputs.contains("window-icon:0"), "Custom style mounts native icon path inputs");
      focusControl("window-icon:0");
      pasteControl("window-icon:0", "relative.svg");
      auto& apply = widget("user-account-button:window-icon-apply:0");
      float x = 0, y = 0;
      Node::mapToScene(&apply, 0, 0, x, y);
      m_settingsScroll += y - layout::Settings::viewportTop - Style::spaceLg;
      break;
    }
    case 5:
      click("user-account-button:window-icon-apply:0");
      require(
          m_preferences.windowControls.icons[0].empty() && !m_windowIconErrors[0].empty(),
          "Apply rejects a relative path and keeps the previous icon preference"
      );
      m_inputs.at("window-icon:0")->setValue("");
      pasteControl("window-icon:0", "/tmp/offline-window-icon.svg");
      click("user-account-button:window-icon-apply:0");
      require(
          m_preferences.windowControls.icons[0] == "/tmp/offline-window-icon.svg",
          "Native Apply accepts an absolute local path without changing profile drafts"
      );
      break;
    case 6: {
      const std::string svg =
          R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="M3 10H21V14H3Z"/></svg>)";
      const auto image = WindowIcons::decode(
          {reinterpret_cast<const std::uint8_t*>(svg.data()), svg.size()}, true,
          static_cast<int>(layout::Titlebar::iconSize), "#8f80d4"
      );
      require(image.has_value(), "Offline generated custom SVG uses the normal icon decoder");
      m_windowIconTextures[0] =
          m_window.context().textureManager().loadFromRgba(image->rgba.data(), image->width, image->height);
      require(m_windowIconTextures[0].valid(), "Custom icon uploads through the existing texture manager");
      break;
    }
    case 7:
      require(m_windowIconTextures[0].valid(), "A custom icon survives a scene rebuild");
      m_preferences.theme = m_preferences.theme == "light" ? "dark" : "light";
      break;
    case 8:
      require(
          !m_windowIconTextures[0].valid(), "Palette changes discard stale custom icon colors before rerasterization"
      );
      click("user-account-button:window-icon-reset:0");
      require(m_preferences.windowControls.icons[0].empty(), "Native per-icon Reset restores the standard fallback");
      windowControlPreference("window-icon-apply:99");
      windowControlPreference("window-order:-1");
      require(m_preferences.windowControls.order == 0, "Invalid setting actions cannot alter the current layout");
      m_settingsScroll = std::numeric_limits<float>::max();
      break;
    case 9:
      click("user-account-button:window-reset");
      require(
          m_preferences.windowControls == WindowControlPreferences{},
          "Native Restore defaults resets all titlebar customizations"
      );
      selectSettingsPage("appearance");
      selectSettingsPage("advanced");
      break;
    case 10:
      require(
          m_preferences.windowControls == WindowControlPreferences{} && !m_profileDirty,
          "Navigation retains local titlebar choices without modifying an account profile"
      );
      m_window.setUiScale(1.6F);
      break;
    case 11:
      windowControlPreference("window-left");
      choose("window-order", 1);
      choose("window-style", 3);
      break;
    case 12:
      require(
          m_preferences.windowControls.left && titleHits().front().action == Action::Close,
          "Narrow/zoomed layout keeps left controls in the configured order"
      );
      m_settingsScroll = std::numeric_limits<float>::max();
      break;
    case 13: {
      auto& reset = widget("user-account-button:window-reset");
      float left = 0, top = 0, right = 0, bottom = 0;
      Node::transformedBounds(&reset, left, top, right, bottom);
      require(
          left >= 0 && right <= m_window.width() && top >= layout::Settings::viewportTop && bottom <= m_window.height(),
          "Measured custom icon fields and actions remain reachable at narrow widths"
      );
      click("user-account-button:window-reset");
      break;
    }
    case 14:
      require(m_preferences.windowControls == WindowControlPreferences{}, "Reset also works in the narrow layout");
      m_window.setUiScale(1);
      selectSettingsPage("advanced");
      break;
    case 15:
      require(
          !m_profileDirty && m_preferences.windowControls == WindowControlPreferences{},
          "Titlebar settings preserve profile state across native controls, custom icons and scaling"
      );
      std::clog << "Offline window controls, icon previews, native settings and drag geometry checks passed\n";
      break;
    }
    m_window.invalidate();
  }
} // namespace app
