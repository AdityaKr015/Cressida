#include "app/client_application.h"
#include "app/theme.h"
#include "render/scene/input_area.h"
#include "ui/controls/button.h"
#include "ui/controls/toggle.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace app {
  using namespace theme;
  namespace {
    struct SettingsPage {
      const char* id;
      const char* name;
      const char* group;
      float height;
    };
    constexpr SettingsPage pages[] = {
        {"account", "My Account", "ACCOUNT", 530},
        {"profile", "Profiles", "ACCOUNT", 540},
        {"presence", "Status", "ACCOUNT", 460},
        {"privacy", "Privacy & Safety", "ACCOUNT", 790},
        {"content", "Content & Social", "ACCOUNT", 510},
        {"friends", "Friend Requests", "ACCOUNT", 360},
        {"authorized", "Authorized Apps", "ACCOUNT", 400},
        {"devices", "Devices", "ACCOUNT", 400},
        {"connections", "Connections", "ACCOUNT", 400},
        {"subscriptions", "Subscriptions & Billing", "PAYMENT", 350},
        {"appearance", "Appearance", "APP SETTINGS", 440},
        {"accessibility", "Accessibility", "APP SETTINGS", 400},
        {"voice", "Voice & Video", "APP SETTINGS", 350},
        {"text", "Text & Images", "APP SETTINGS", 410},
        {"notifications", "Notifications", "APP SETTINGS", 920},
        {"keybinds", "Keybinds", "APP SETTINGS", 530},
        {"language", "Language", "APP SETTINGS", 350},
        {"advanced", "Advanced", "APP SETTINGS", 350},
        {"activity", "Activity Privacy", "ACTIVITY SETTINGS", 480},
        {"games", "Registered Games", "ACTIVITY SETTINGS", 350},
        {"overlay", "Game Overlay", "ACTIVITY SETTINGS", 350},
        {"expressions", "Expressions", "CRESSIDA", 360}
    };
    std::string folded(std::string text) {
      for (auto& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return text;
    }
    bool listPage(const std::string& id) { return id == "connections" || id == "devices" || id == "authorized"; }
  } // namespace
  void ClientApplication::clearAccountSettings() {
    m_accountSettings.clear();
    m_settingsDraft.clear();
    m_settingsLists.clear();
    m_settingsListErrors.clear();
    m_loadingSettingsLists.clear();
    m_settingsLoaded = m_loadingSettings = m_savingSettings = false;
  }
  void ClientApplication::selectSettingsPage(const std::string& page) {
    m_scrolling.stopAll();
    if (std::ranges::none_of(pages, [&](const auto& item) { return item.id == page; }))
      return;
    m_settingsPage = page;
    m_settingsScroll = 0;
    m_settingsField.clear();
    m_controlInput.setFocus(nullptr);
    m_focusedInput.clear();
    float rowY = 0;
    std::string group;
    const auto query = folded(m_settingsFields["search"]);
    const float navHeight = std::max(30.0F, static_cast<float>(m_window.height()) - 222);
    for (const auto& entry : pages) {
      if (!query.empty() && folded(std::string(entry.name) + " " + entry.group).find(query) == std::string::npos)
        continue;
      if (group != entry.group) {
        group = entry.group;
        rowY += 30;
      }
      if (page == entry.id) {
        if (rowY < m_settingsNavScroll)
          m_settingsNavScroll = rowY;
        else if (rowY + 32 > m_settingsNavScroll + navHeight)
          m_settingsNavScroll = rowY + 32 - navHeight;
        break;
      }
      rowY += 36;
    }
    if (listPage(page) && !m_demo && !m_settingsLists.contains(page) && !m_loadingSettingsLists.contains(page)) {
      if (m_client.settingsList(page)) {
        m_loadingSettingsLists.insert(page);
        m_settingsListErrors.erase(page);
      } else
        m_settingsListErrors[page] = "Could not request this list. Check the connection.";
    }
  }
  void ClientApplication::resetProfile() {
    m_settingsFields["display_name"] = m_account.displayName;
    m_settingsFields["pronouns"] = m_account.pronouns;
    m_settingsFields["bio"] = m_account.bio;
    m_settingsField.clear();
    m_profileDirty = false;
  }
  void ClientApplication::openSettings() {
    m_scrolling.stopAll();
    closeGifPicker();
    m_settingsOpen = true;
    m_presenceMenu = false;
    m_emojiMenu = false;
    m_settingsScroll = 0;
    m_settingsNavScroll = 0;
    m_fieldFocused = false;
    m_dragDraft = m_dragMessage = false;
    resetProfile();
    m_settingsFields["custom_status"] = m_presence.text;
    m_settingsStatus.clear();
    m_controlInput.setFocus(nullptr);
    m_focusedInput.clear();
    m_loadingProfile = !m_demo && m_client.profile();
    m_loadingSettings = !m_demo && m_client.accountSettings();
    if (m_demo) {
      m_accountSettings = discord::parseAccountSettings(nlohmann::json::object());
      m_settingsDraft = m_accountSettings;
      m_settingsLoaded = true;
    }
    selectSettingsPage(m_settingsPage);
  }
  void ClientApplication::savePreferences() {
    m_messageLayouts.clear();
    if (!m_demo)
      m_sessionStore.savePreferences(m_account.id, m_preferences);
    m_window.invalidate();
  }
  void ClientApplication::setPresence(const std::string& status, bool custom) {
    m_presenceMenu = false;
    if (m_demo) {
      m_settingsStatus = "Demo mode is read-only.";
      return;
    }
    if (m_client.setPresence(status, custom ? std::optional(m_settingsFields["custom_status"]) : std::nullopt)) {
      m_desiredPresence = status;
      m_settingsStatus = "Saving status…";
    } else
      m_settingsStatus = "Could not update status. Check the connection.";
  }
  void ClientApplication::settingsInput(
      Node& parent, float x, float y, float width, float height, std::string caption, std::string id
  ) {
    label(parent, x, y, width, std::move(caption), 11, muted, FontWeight::SemiBold);
    inputControl(parent, x, y + 23, width, height, "settings:" + id, m_settingsFields[id], {}, id == "bio");
  }
  void ClientApplication::drawPresenceMenu(Node& root, float h) {
    m_hits.push_back({0, 36, static_cast<float>(m_window.width()), h - 36, Action::TogglePresence, {}});
    const float x = 90, y = h - 338;
    rectangle(root, x, y, 258, 254, background, 12);
    label(root, x + 16, y + 14, 220, m_account.name, 14, foreground, FontWeight::SemiBold);
    int index = 0;
    for (const auto& status : {"online", "idle", "dnd", "invisible"}) {
      const float sy = y + 45 + index++ * 40;
      if (hovered(x + 8, sy, 242, 36))
        rectangle(root, x + 8, sy, 242, 36, panel, 6);
      rectangle(root, x + 18, sy + 12, 10, 10, presenceColor(status), 5);
      label(root, x + 40, sy + 9, 190, std::string(presenceName(status)), 14, foreground);
      m_hits.push_back({x + 8, sy, 242, 36, Action::SetPresence, status});
    }
    button(root, {x + 10, y + 210, 238, 32, Action::SettingsPage, "presence"}, "Set a custom status");
  }
  void ClientApplication::drawSettings(Node& root, float w, float h) {
    std::erase_if(m_hits, [](const auto& hit) {
      return hit.action != Action::Close && hit.action != Action::Minimize && hit.action != Action::Maximize;
    });
    rectangle(root, 0, 36, w, h - 36, background);
    const float frameX = std::max(16.0F, (w - 1100) / 2), navWidth = w < 760 ? 190 : 224;
    const float contentX = frameX + navWidth + 32, contentWidth = std::max(180.0F, std::min(w - contentX - 78, 730.0F));
    m_settingsNavRight = contentX - 16;
    rectangle(root, 0, 36, m_settingsNavRight, h - 36, sidebar);
    label(root, frameX + 12, 58, navWidth - 24, "SETTINGS", 11, muted, FontWeight::SemiBold);
    inputControl(
        root, frameX + 4, 84, navWidth - 8, 36, "settings:search", m_settingsFields["search"], "Search settings"
    );
    struct NavRow {
      const SettingsPage* page;
      float y;
      bool heading;
    };
    std::vector<NavRow> navRows;
    float navHeight = 0;
    std::string group;
    const auto query = folded(m_settingsFields["search"]);
    for (const auto& page : pages) {
      if (!query.empty() && folded(std::string(page.name) + " " + page.group).find(query) == std::string::npos)
        continue;
      if (group != page.group) {
        group = page.group;
        navRows.push_back({&page, navHeight, true});
        navHeight += 30;
      }
      navRows.push_back({&page, navHeight, false});
      navHeight += 36;
    }
    const float navY = 132, navH = std::max(30.0F, h - navY - 90);
    m_scrolling.limit(m_settingsNavScroll, std::max(0.0F, navHeight - navH));
    auto nav = std::make_unique<Node>();
    nav->setPosition(frameX, navY);
    nav->setSize(navWidth, navH);
    nav->setClipChildren(true);
    for (const auto& row : navRows) {
      const auto y = row.y - m_settingsNavScroll;
      if (y < 0 || y + 32 > navH)
        continue;
      if (row.heading)
        label(*nav, 12, y + 8, navWidth - 24, row.page->group, 10, muted, FontWeight::SemiBold);
      else {
        const auto controlId = "nav:" + std::string(row.page->id);
        std::unique_ptr<Node> saved;
        if (auto at = m_detachedInputs.find(controlId); at != m_detachedInputs.end()) {
          saved = std::move(at->second);
          m_detachedInputs.erase(at);
        }
        auto item = saved ? std::unique_ptr<Button>(static_cast<Button*>(saved.release())) : std::make_unique<Button>();
        m_widgets[controlId] = item.get();
        item->setText(row.page->name);
        item->setFontSize(13);
        item->setVariant(m_settingsPage == row.page->id ? ButtonVariant::TabActive : ButtonVariant::Tab);
        item->setContentAlign(ButtonContentAlign::Start);
        item->setPosition(4, y);
        item->setControlHeight(32);
        item->setFrameSize(navWidth - 8, 32);
        item->setOnClick([this, id = std::string(row.page->id)] {
          selectSettingsPage(id);
          m_window.invalidate();
        });
        nav->addChild(std::move(item));
      }
    }
    if (navRows.empty())
      label(*nav, 12, 12, navWidth - 24, "No matching settings", 12, muted);
    root.addChild(std::move(nav));
    if (navHeight > navH)
      rectangle(
          root, frameX + navWidth - 3, navY + (navH - 32) * m_settingsNavScroll / (navHeight - navH), 3, 32, raised, 2
      );
    button(root, {frameX + 4, h - 70, navWidth - 8, 36, Action::SignOut, {}}, "Log Out");
    iconButton(root, {w - 60, 52, 32, 32, Action::CloseSettings, {}}, "x", "Close settings");
    label(root, w - 61, 98, 44, "ESC", 10, muted);
    const auto selected = std::ranges::find_if(pages, [&](const auto& page) { return m_settingsPage == page.id; });
    const std::string title = selected == std::end(pages) ? "Settings" : selected->name;
    label(root, contentX, 64, contentWidth, title, 24, foreground, FontWeight::Bold);
    const float viewportY = 110, viewportH = std::max(40.0F, h - viewportY - 112);
    float pageHeight = selected == std::end(pages) ? 400 : selected->height;
    if (listPage(m_settingsPage))
      pageHeight = 150 + m_settingsLists[m_settingsPage].size() * 96;
    m_scrolling.limit(m_settingsScroll, std::max(0.0F, pageHeight - viewportH));
    auto page = std::make_unique<Node>();
    page->setPosition(contentX, viewportY);
    page->setSize(contentWidth, viewportH);
    page->setClipChildren(true);
    auto body = std::make_unique<Node>();
    body->setPosition(0, -m_settingsScroll);
    // Attach before building controls so their native hit regions include all parent offsets.
    auto* node = body.get();
    page->addChild(std::move(body));
    auto& p = *node;
    const auto firstHit = m_hits.size();
    const float width = contentWidth;
    auto toggle = [&](float y, std::string name, bool enabled, std::string id, bool remote = false,
                      std::string description = {}) {
      rectangle(p, 0, y, width, description.empty() ? 64 : 86, sidebar, 9);
      label(p, 16, y + 17, width - 98, name, 14, foreground, FontWeight::SemiBold);
      if (!description.empty())
        label(p, 16, y + 43, width - 98, description, 12, muted, FontWeight::Normal, 2);
      const auto controlId = "toggle:" + id;
      std::unique_ptr<Node> saved;
      if (auto at = m_detachedInputs.find(controlId); at != m_detachedInputs.end()) {
        saved = std::move(at->second);
        m_detachedInputs.erase(at);
      }
      auto control =
          saved ? std::unique_ptr<Toggle>(static_cast<Toggle*>(saved.release())) : std::make_unique<Toggle>();
      m_widgets[controlId] = control.get();
      control->setCheckedImmediate(enabled);
      control->setEnabled(!remote || (m_settingsLoaded && !m_loadingSettings && !m_savingSettings));
      control->setPosition(width - 62, y + 19);
      control->setFrameSize(40, 24);
      control->setOnChange([this, id, remote](bool) {
        activate({0, 0, 0, 0, remote ? Action::AccountPreference : Action::Preference, id});
        m_window.invalidate();
      });
      p.addChild(std::move(control));
    };
    auto accountToggle = [&](float y, std::string name, std::string key, std::string description = {}) {
      const auto value = m_settingsDraft.contains(key) ? m_settingsDraft.at(key) : 0;
      toggle(y, std::move(name), value != 0, key, true, std::move(description));
    };
    auto choices = [&](float y, const std::string& key, const std::vector<std::string>& names) {
      for (std::size_t i = 0; i < names.size(); ++i) {
        const bool active = m_settingsDraft.contains(key) && m_settingsDraft.at(key) == static_cast<int>(i);
        rectangle(p, 0, y + i * 42, width, 36, active ? raised : sidebar, 7);
        label(p, 14, y + i * 42 + 10, width - 40, names[i], 13, foreground);
        if (active)
          label(p, width - 26, y + i * 42 + 9, 20, "✓", 14, accent);
        if (m_settingsLoaded && !m_loadingSettings && !m_savingSettings)
          m_hits.push_back(
              {contentX, viewportY - m_settingsScroll + y + i * 42, width, 36, Action::AccountPreference,
               key + ":" + std::to_string(i)}
          );
      }
    };
    if (m_settingsPage == "account") {
      rectangle(p, 0, 0, width, 240, sidebar, 12);
      avatar(p, 20, 20, 72, m_account.avatar, m_account.name);
      label(p, 110, 25, width - 130, m_account.name, 22, foreground, FontWeight::Bold);
      label(p, 110, 61, width - 130, "@" + m_account.username, 14, muted);
      label(p, 22, 118, width - 44, "USERNAME", 10, muted, FontWeight::SemiBold);
      label(p, 22, 139, width - 44, m_account.username, 14, foreground);
      label(p, 22, 179, width - 44, "ACCOUNT ID", 10, muted, FontWeight::SemiBold);
      label(p, 22, 200, width - 44, m_account.id, 14, foreground);
      button(p, {0, 260, 150, 38, Action::SettingsPage, "profile"}, "Edit profile", true);
      label(p, 0, 324, width, "SIGN-IN", 10, muted, FontWeight::SemiBold);
      label(
          p, 0, 346, width, m_demo ? "Demo does not access or change saved sign-in." : m_loginStorageStatus, 13, muted,
          FontWeight::Normal, 3
      );
      label(
          p, 0, 418, width,
          "Password, email and multi-factor authentication changes still require Discord. Connections and devices have "
          "their own settings pages.",
          13, muted, FontWeight::Normal, 3
      );
    } else if (m_settingsPage == "profile") {
      avatar(p, 0, 0, 58, m_account.avatar, m_account.name);
      label(p, 76, 17, width - 76, m_account.name, 19, foreground, FontWeight::SemiBold);
      settingsInput(p, 0, 82, width, 44, "DISPLAY NAME", "display_name");
      settingsInput(p, 0, 164, width, 44, "PRONOUNS", "pronouns");
      settingsInput(p, 0, 246, width, 112, "ABOUT ME", "bio");
      label(p, 0, 390, width, "Display name: 32 characters · Pronouns: 40 · About me: 190", 11, muted);
      button(p, {0, 430, 148, 38, Action::SaveProfile, {}}, m_savingProfile ? "Saving…" : "Save Changes", true);
      button(p, {160, 430, 100, 38, Action::ResetProfile, {}}, "Reset");
      if (m_loadingProfile)
        label(p, 0, 488, width, "Loading profile…", 12, muted);
    } else if (m_settingsPage == "presence") {
      int index = 0;
      for (const auto& status : {"online", "idle", "dnd", "invisible"}) {
        const float y = index++ * 48;
        button(
            p, {0, y, width, 40, Action::SetPresence, status}, std::string(presenceName(status)), false,
            m_presence.status == status, true
        );
        rectangle(p, width - 28, y + 14, 11, 11, presenceColor(status), 5);
      }
      label(
          p, 0, 208, width,
          "Invisible makes this session appear offline. Other active devices can still affect your account's presence.",
          12, muted, FontWeight::Normal, 3
      );
      settingsInput(p, 0, 278, width, 44, "CUSTOM STATUS", "custom_status");
      button(p, {0, 365, 156, 38, Action::SaveCustomStatus, {}}, "Save Status", true);
    } else if (m_settingsPage == "privacy") {
      label(
          p, 0, 0, width, "Defaults for new servers. Existing server overrides are preserved.", 12, muted,
          FontWeight::Normal, 2
      );
      accountToggle(56, "Block DMs from new server members", "restrict_new_guild_dms");
      accountToggle(132, "Disable requests from new server members", "restrict_message_requests");
      accountToggle(208, "Contact discovery", "contact_sync", "Allow Discord to use your contact sync preference.");
      accountToggle(306, "Hide legacy username badge", "hide_legacy_username");
      label(p, 0, 402, width, "PROFILE VISIBILITY", 11, muted, FontWeight::SemiBold);
      choices(
          432, "profile_visibility",
          {"Discord default", "Friends only", "Friends and small servers", "Friends and all servers"}
      );
    } else if (m_settingsPage == "content") {
      label(p, 0, 0, width, "DIRECT MESSAGE SPAM FILTER", 11, muted, FontWeight::SemiBold);
      choices(30, "dm_spam_filter", {"Discord default", "Do not filter", "Filter non-friends", "Filter everyone"});
      accountToggle(228, "Conversation safety warnings", "conversation_warnings");
      label(
          p, 0, 324, width,
          "These are Discord account preferences. This client does not yet implement sensitive-media blurring or "
          "message-request folders.",
          13, muted, FontWeight::Normal, 4
      );
    } else if (m_settingsPage == "friends") {
      label(p, 0, 0, width, "WHO CAN SEND YOU A FRIEND REQUEST", 11, muted, FontWeight::SemiBold);
      const auto flags = m_settingsDraft.contains("friend_sources") ? m_settingsDraft.at("friend_sources") : 14;
      toggle(40, "Everyone", flags & 8, "friend_sources:8", true);
      toggle(116, "Friends of friends", flags & 2, "friend_sources:2", true);
      toggle(192, "Server members", flags & 4, "friend_sources:4", true);
    } else if (m_settingsPage == "notifications") {
      label(
          p, 0, 0, width,
          "Synced Discord account preferences. Desktop notification delivery and notification sounds are not "
          "implemented in this client yet.",
          12, muted, FontWeight::Normal, 3
      );
      accountToggle(76, "In-app notifications", "in_app_notifications");
      accountToggle(152, "Friends coming online", "friend_online_notifications");
      accountToggle(228, "Profile update notifications", "profile_notifications");
      accountToggle(304, "Upcoming server events", "server_event_notifications");
      accountToggle(380, "Notify friends when I stream", "notify_friends_stream");
      accountToggle(456, "Notify friends when I update my profile", "notify_friends_profile");
      accountToggle(532, "Notify friends when I come online", "notify_friends_online");
      label(p, 0, 634, width, "REACTION NOTIFICATIONS", 11, muted, FontWeight::SemiBold);
      choices(664, "reaction_notifications", {"All conversations", "Direct messages only", "Off"});
    } else if (m_settingsPage == "activity") {
      accountToggle(
          0, "Share current activity", "show_activity",
          "Sync the account preference. Automatic game detection is not available yet."
      );
      accountToggle(102, "Show recent games", "recent_games");
      accountToggle(178, "Allow friends to join activities", "join_friends");
      accountToggle(254, "Allow voice participants to join", "join_voice");
    } else if (listPage(m_settingsPage)) {
      label(
          p, 0, 0, width,
          "View your account's " + folded(title) + ". Managing or removing them still requires Discord.", 13, muted,
          FontWeight::Normal, 2
      );
      if (m_demo)
        label(p, 0, 76, width, "Demo mode does not load account data.", 14, muted);
      else if (m_loadingSettingsLists.contains(m_settingsPage))
        label(p, 0, 76, width, "Loading…", 14, muted);
      else if (m_settingsListErrors.contains(m_settingsPage))
        label(p, 0, 76, width, m_settingsListErrors[m_settingsPage], 13, muted, FontWeight::Normal, 3);
      else if (m_settingsLists[m_settingsPage].empty())
        label(p, 0, 76, width, "Nothing to show.", 14, muted);
      int index = 0;
      for (const auto& entry : m_settingsLists[m_settingsPage]) {
        const auto y = 80 + index++ * 96.0F;
        rectangle(p, 0, y, width, 82, sidebar, 9);
        label(p, 16, y + 14, width - 32, entry.name, 15, foreground, FontWeight::SemiBold);
        label(p, 16, y + 42, width - 32, entry.detail, 12, muted, FontWeight::Normal, 2);
      }
    } else if (m_settingsPage == "keybinds") {
      int row = 0;
      for (auto [key, meaning] :
           {std::pair{"Ctrl + ,", "Open settings"},
            {"Ctrl + + / − / 0", "Zoom in / out / reset"},
            {"Enter / Shift + Enter", "Send message / new line"},
            {"Ctrl + A / C / X / V", "Select all / copy / cut / paste"},
            {"Ctrl + Z / Shift + Ctrl + Z", "Undo / redo"},
            {"Ctrl + ← / →", "Move by word"},
            {"Ctrl + Backspace / Delete", "Delete previous / next word"},
            {"Escape", "Close settings or image viewer"},
            {"Ctrl + Q", "Quit"}}) {
        rectangle(p, 0, row * 48, width, 42, sidebar, 6);
        label(p, 14, row * 48 + 13, width * 0.48F, key, 12, accent);
        label(p, width * 0.49F, row++ * 48 + 13, width * 0.49F - 12, meaning, 12, foreground);
      }
      label(p, 0, 460, width, "Custom keybinds are not available yet.", 12, muted);
    } else if (m_settingsPage == "accessibility") {
      label(p, 0, 0, width, "TEXT AND INTERFACE", 11, muted, FontWeight::SemiBold);
      label(
          p, 0, 34, width, "Adjust message text size, interface scale and theme in Appearance.", 14, foreground,
          FontWeight::Normal, 3
      );
      button(p, {0, 114, 180, 38, Action::SettingsPage, "appearance"}, "Open Appearance", true);
      label(
          p, 0, 188, width,
          "Inputs support keyboard selection, word navigation and undo. Screen-reader announcements and reduced-motion "
          "preferences are not implemented yet.",
          13, muted, FontWeight::Normal, 4
      );
    } else if (m_settingsPage == "advanced") {
      label(p, 0, 0, width, "Native Wayland · OpenGL ES · C++23", 15, foreground, FontWeight::SemiBold);
      label(
          p, 0, 54, width,
          "Interface scale follows Appearance. Window decorations follow the compositor's preference. Hardware "
          "rendering is enabled. Additional developer and diagnostics settings are not available yet.",
          13, muted, FontWeight::Normal, 5
      );
    } else if (m_settingsPage == "appearance") {
      label(p, 0, 0, width, "THEME", 11, muted, FontWeight::SemiBold);
      int index = 0;
      const float part = (width - 16) / 3;
      for (const auto& name : {"dark", "light", "midnight"})
        button(
            p, {index++ * (part + 8), 28, part, 40, Action::Preference, "theme:" + std::string(name)}, name, false,
            m_preferences.theme == name, true
        );
      label(p, 0, 100, width, "MESSAGE TEXT SIZE", 11, muted, FontWeight::SemiBold);
      button(p, {0, 130, 42, 36, Action::Preference, "font-"}, "−");
      label(p, 60, 139, 80, std::to_string(static_cast<int>(m_preferences.messageSize)) + " px", 14, foreground);
      button(p, {145, 130, 42, 36, Action::Preference, "font+"}, "+");
      label(p, 0, 200, width, "UI SCALE", 11, muted, FontWeight::SemiBold);
      button(p, {0, 230, 42, 36, Action::Preference, "zoom-"}, "−");
      button(
          p, {54, 230, 90, 36, Action::Preference, "zoom0"},
          std::to_string(static_cast<int>(m_window.uiScale() * 100)) + "%"
      );
      button(p, {156, 230, 42, 36, Action::Preference, "zoom+"}, "+");
      toggle(300, "Compact message spacing", m_preferences.compact, "compact");
      label(
          p, 0, 362, width, "Appearance changes apply immediately and are saved for this account on this device.", 12,
          muted, FontWeight::Normal, 3
      );
    } else if (m_settingsPage == "expressions") {
      toggle(
          0, "FakeNitro emoji", m_preferences.fakeNitroEmojis, "fake_nitro_emojis", false,
          "Send unsupported server emoji as image links."
      );
      label(
          p, 0, 108, width,
          "External and animated emoji use named image links when your account cannot send them normally. "
          "Other clients may show a link or image preview; channel embed permissions still apply.",
          13, muted, FontWeight::Normal, 4
      );
      label(
          p, 0, 220, width,
          "Applies to messages and edits. Reactions use Discord's normal rules. Saved only for this account in "
          "Cressida.",
          12, muted, FontWeight::Normal, 3
      );
    } else if (m_settingsPage == "text") {
      toggle(0, "Show link previews and embeds", m_preferences.showEmbeds, "embeds");
      toggle(76, "Show image attachments", m_preferences.showImages, "images");
      toggle(152, "Automatically play GIFs", m_preferences.autoplayGifs, "autoplay_gifs");
      label(
          p, 0, 252, width,
          "GIFs play while this window is focused, or on hover when autoplay is off. Hidden images remain available as "
          "links. These preferences only affect this client.",
          13, muted, FontWeight::Normal, 3
      );
    } else {
      rectangle(p, 0, 0, width, 194, sidebar, 12);
      label(p, 20, 20, width - 40, "Not available yet", 18, foreground, FontWeight::SemiBold);
      const std::string detail = m_settingsPage == "voice"
          ? "Voice, video, microphone selection and audio processing still need the voice backend."
          : m_settingsPage == "subscriptions"
          ? "Nitro, subscriptions, gifts and payment management still require Discord."
          : m_settingsPage == "language"
          ? "The interface currently uses English. Translation and language selection are not implemented yet."
          : m_settingsPage == "games"
          ? "Automatic game detection and manually registered games are not implemented yet."
          : "The in-game overlay is not implemented yet.";
      label(p, 20, 62, width - 40, detail, 14, muted, FontWeight::Normal, 4);
      button(p, {0, 222, 220, 38, Action::OpenUrl, "https://discord.com/channels/@me"}, "Open Discord in browser");
    }
    for (auto i = firstHit; i < m_hits.size(); ++i) {
      auto& hit = m_hits[i];
      const float end = std::min(viewportY + viewportH, hit.y + hit.height);
      hit.y = std::max(hit.y, viewportY);
      hit.height = std::max(0.0F, end - hit.y);
    }
    root.addChild(std::move(page));
    if (m_settingsDraft != m_accountSettings && m_settingsLoaded) {
      rectangle(root, contentX, h - 92, contentWidth, 48, panel, 9);
      label(root, contentX + 14, h - 76, std::max(80.0F, contentWidth - 270), "Unsaved changes", 12, foreground);
      button(root, {contentX + contentWidth - 244, h - 86, 84, 36, Action::ResetAccountSettings, {}}, "Reset");
      button(
          root, {contentX + contentWidth - 150, h - 86, 140, 36, Action::SaveAccountSettings, {}},
          m_savingSettings ? "Saving…" : "Save Changes", true
      );
    }
    if (!m_settingsStatus.empty())
      label(root, contentX, h - 31, contentWidth, m_settingsStatus, 12, muted, FontWeight::Normal, 2);
  }
  void ClientApplication::settingsKey(const Key& key) {
    if (key.symbol == XKB_KEY_Escape) {
      m_settingsOpen = false;
      m_settingsField.clear();
      m_controlInput.setFocus(nullptr);
      m_focusedInput.clear();
      m_window.invalidate();
    }
  }
} // namespace app
