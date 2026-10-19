#include "app/client_application.h"
#include "app/message_group.h"
#include "app/theme.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace app {
  void ClientApplication::demoUserPreferencesStep() {
    const auto require = [](bool ok, const char* reason) {
      if (!ok)
        throw std::runtime_error(reason);
    };
    const auto click = [&](const std::string& id) {
      const auto found = m_widgets.find(id);
      require(found != m_widgets.end() && found->second->parent(), "Preference control is retained and attached");
      float x = found->second->width() / 2, y = found->second->height() / 2;
      for (const auto* node = found->second; node; node = node->parent()) {
        x += node->x();
        y += node->y();
      }
      m_window.click(x, y + m_window.titlebarHeight() - layout::Window::titlebarHeight, 272);
      m_window.released();
    };
    const auto record = [&](const std::string& name) {
      click("settings-button:" + std::to_string(static_cast<int>(Action::Preference)) + ":keybind-record:" + name);
    };
    const auto step = m_demoInteractionStep++;
    std::clog << "Offline client preference frame " << step << '\n';
    switch (step) {
    case 0:
      require(m_settingsPage == "keybinds", "Shortcuts replay starts on its native settings page");
      record("settings");
      break;
    case 1:
      require(m_shortcutCapture == "settings", "Record button begins keyboard capture");
      onKey({.symbol = XKB_KEY_s, .control = true, .alt = true});
      require(
          m_shortcutCapture.empty() && shortcutLabel("settings") == "Ctrl+Alt+s",
          "Native keyboard recording saves a chord"
      );
      break;
    case 2:
      activate({0, 0, 0, 0, Action::CloseSettings, {}});
      onKey({.symbol = XKB_KEY_comma, .control = true});
      require(!m_settingsOpen, "The old shortcut stops firing after remapping");
      onKey({.symbol = XKB_KEY_s, .control = true, .alt = true});
      require(m_settingsOpen, "The recorded shortcut opens settings");
      selectSettingsPage("keybinds");
      break;
    case 3:
      record("settings");
      onKey({.symbol = XKB_KEY_f, .control = true});
      require(
          m_shortcutCapture == "settings" && m_settingsStatus.find("already assigned") != std::string::npos,
          "Shortcut conflicts retain the preceding binding and recording state"
      );
      onKey({.symbol = XKB_KEY_Escape});
      require(m_shortcutCapture.empty() && m_settingsOpen, "Escape cancels recording before closing settings");
      break;
    case 4:
      record("settings");
      onKey({.symbol = XKB_KEY_BackSpace});
      require(shortcutLabel("settings") == "Unassigned", "Backspace clears the captured shortcut");
      extendedPreference("keybind-reset:settings");
      require(shortcutLabel("settings") == "Ctrl+comma", "Reset restores the original shortcut");
      selectSettingsPage("accessibility");
      break;
    case 5:
      click("toggle:reduced_motion");
      require(m_preferences.reducedMotion, "Reduced motion toggles through the native settings control");
      extendedPreference("saturation:0");
      extendedPreference("role_color_mode:1");
      break;
    case 6:
      require(
          std::abs(theme::accent.r - theme::accent.g) < .0001F && std::abs(theme::accent.g - theme::accent.b) < .0001F,
          "Saturation changes the actual active interface palette"
      );
      require(m_preferences.roleColors && m_preferences.roleColorMode == 1, "Color dots retain role metadata");
      extendedPreference("role_color_mode:2");
      require(!m_preferences.roleColors, "Disabling role colors updates the shared renderer preference");
      extendedPreference("show_reactions");
      require(
          reactionStrip(nullptr, m_messages.front(), 0, 0, 400) == 0,
          "The reaction preference removes rendered strips and their reserved height"
      );
      extendedPreference("spoiler_mode:2");
      require(spoilersVisible("not-explicitly-revealed"), "Always-visible spoilers affect actual message rendering");
      break;
    case 7:
      extendedPreference("message_group_spacing:4");
      require(
          messageRowPadding(true, false, false, m_preferences.messageGroupSpacing) == 4
              && messageRowPadding(true, true, false, m_preferences.messageGroupSpacing) == 0,
          "Group spacing preserves zero space between continuation rows"
      );
      extendedPreference("keybind-record:settings");
      onKey({.symbol = XKB_KEY_x, .text = "x"});
      require(!m_shortcutCapture.empty(), "Unmodified typing is rejected as a global shortcut");
      onKey({.symbol = XKB_KEY_Escape});
      selectSettingsPage("notifications");
      break;
    case 8:
      click("toggle:desktop_notifications");
      require(!m_preferences.desktopNotifications, "Desktop notification control changes real delivery gating");
      extendedPreference("notification_privacy:2");
      require(m_preferences.notificationPrivacy == 2, "Notification privacy stores the selected disclosure level");
      break;
    case 9:
      extendedPreference("saturation:100");
      extendedPreference("reduced_motion");
      extendedPreference("show_reactions");
      extendedPreference("role_color_mode:0");
      extendedPreference("spoiler_mode:0");
      require(
          !m_preferences.reducedMotion && m_preferences.showReactions && m_preferences.roleColors,
          "Client preferences can be restored independently"
      );
      selectSettingsPage("privacy");
      break;
    case 10:
      require(theme::accent == rgbHex(theme::palette.accent), "Restoring saturation restores exact theme colors");
      focusControl("settings:search");
      m_profileDirty = false;
      onKey({.symbol = XKB_KEY_p, .text = "p"});
      require(
          !m_profileDirty && m_settingsFields["search"] == "p", "Native settings search never edits account profiles"
      );
      break;
    case 11:
      extendedPreference("keybind-reset-all");
      require(m_preferences.keybinds.empty() && m_shortcutCapture.empty(), "Reset all restores default keybindings");
      m_window.invalidate();
      break;
    }
    m_window.invalidate();
  }
} // namespace app
