#include "app/client_application.h"
#include "app/theme.h"
#include "render/scene/input_area.h"
#include "render/scene/rect_node.h"
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
      const char* keywords = "";
    };
    constexpr float navTop = 124, navBottom = 78, navRowHeight = 30, navHeadingHeight = 26;
    constexpr SettingsPage pages[] = {
        {"cressida", "General", "CRESSIDA",
         "messages deleted edited history logger memory expressions emoji image links name badges server tags role "
         "colors gradients animation"},
        {"account", "My Account", "ACCOUNT"},
        {"profile", "Profiles", "ACCOUNT"},
        {"presence", "Status", "ACCOUNT"},
        {"privacy", "Privacy & Safety", "ACCOUNT"},
        {"content", "Content & Social", "ACCOUNT"},
        {"friends", "Friend Requests", "ACCOUNT"},
        {"authorized", "Authorized Apps", "ACCOUNT"},
        {"devices", "Devices", "ACCOUNT"},
        {"connections", "Connections", "ACCOUNT"},
        {"subscriptions", "Subscriptions & Billing", "PAYMENT"},
        {"appearance", "Appearance", "APP SETTINGS"},
        {"accessibility", "Accessibility", "APP SETTINGS"},
        {"voice", "Voice & Video", "APP SETTINGS"},
        {"text", "Text & Images", "APP SETTINGS"},
        {"notifications", "Notifications", "APP SETTINGS"},
        {"keybinds", "Keybinds", "APP SETTINGS"},
        {"language", "Language", "APP SETTINGS"},
        {"advanced", "Advanced", "APP SETTINGS"},
        {"activity", "Activity Privacy", "ACTIVITY SETTINGS"},
        {"games", "Registered Games", "ACTIVITY SETTINGS"},
        {"overlay", "Game Overlay", "ACTIVITY SETTINGS"}
    };
    std::string folded(std::string text) {
      for (auto& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return text;
    }
    bool matches(const SettingsPage& page, const std::string& query) {
      return query.empty() || folded(std::string(page.name) + " " + page.group + " " + page.keywords).contains(query);
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
    if (page == "expressions" || page == "name-badges" || page == "role-styles") {
      selectSettingsPage("cressida");
      return;
    }
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
    const float navHeight = std::max(30.0F, static_cast<float>(m_window.height()) - navTop - navBottom);
    for (const auto& entry : pages) {
      if (!matches(entry, query))
        continue;
      if (group != entry.group) {
        group = entry.group;
        rowY += navHeadingHeight;
      }
      if (page == entry.id) {
        if (page == "cressida")
          m_settingsNavScroll = 0;
        else if (rowY < m_settingsNavScroll)
          m_settingsNavScroll = rowY;
        else if (rowY + navRowHeight > m_settingsNavScroll + navHeight)
          m_settingsNavScroll = rowY + navRowHeight - navHeight;
        break;
      }
      rowY += navRowHeight;
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
    closeCompletion();
    cancelMessageEdit(false);
    finishSidebarResize();
    closeChannelPanel();
    m_forumSearchTimer.stop();
    closeVideo();
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
      m_accountSettings = api::parseAccountSettings(nlohmann::json::object());
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
    const auto settingsButton = [&](Node& parent, Hit hit, const std::string& caption, bool primary = false,
                                    bool selected = false, bool fill = false, bool centered = true) {
      float ox = 0, oy = 0;
      for (const auto* node = &parent; node && node != m_scene.get(); node = node->parent()) {
        ox += node->x();
        oy += node->y();
      }
      const auto natural = m_window.target().renderer().measureText(caption, 12, FontWeight::Medium);
      if (!fill)
        hit.width = std::min(hit.width, std::ceil(natural.width) + 20);
      hit.y += (hit.height - 26) / 2;
      hit.height = 26;
      const bool enabled = hit.action != Action::None;
      const bool over = enabled && hovered(hit.x + ox, hit.y + oy, hit.width, hit.height);
      auto frame = std::make_unique<RectNode>();
      frame->setPosition(hit.x, hit.y);
      frame->setSize(hit.width, hit.height);
      auto color = primary ? accent : selected ? lerpColor(sidebar, accent, .16F) : over ? raised : panel;
      if (primary && over)
        color = lerpColor(color, foreground, .1F);
      if (!enabled)
        color = lerpColor(sidebar, color, .4F);
      frame->setStyle(
          {.fill = color,
           .border = selected ? accent : withAlpha(muted, .16F),
           .radius = 5,
           .borderWidth = primary ? 0.0F : 1.0F}
      );
      parent.addChild(std::move(frame));
      const float textWidth = std::max(1.0F, std::min(natural.width, hit.width - 16));
      label(
          parent, hit.x + (centered ? (hit.width - textWidth) / 2 : 10),
          hit.y + (26 - (natural.bottom - natural.top)) / 2, textWidth + 1, caption, 12,
          !enabled      ? muted
              : primary ? ink
                        : foreground,
          FontWeight::Medium
      );
      hit.x += ox;
      hit.y += oy;
      m_hits.push_back(std::move(hit));
    };
    const float frameX = std::max(16.0F, (w - 1100) / 2), navWidth = w < 760 ? 180 : 216;
    const float contentLeft = frameX + navWidth + 28;
    const float contentWidth = std::max(180.0F, std::min(w - contentLeft - 68, 730.0F));
    const float contentX = contentLeft + 14, width = contentWidth - 28;
    const auto textHeight = [&](const std::string& text, float size, FontWeight weight, float available) {
      const auto metrics = m_window.target().renderer().measureText(text, size, weight, std::max(1.0F, available), 0);
      return std::max(size + 4, metrics.bottom - metrics.top);
    };
    m_settingsNavRight = frameX + navWidth + 4;
    rectangle(root, 0, 36, m_settingsNavRight, h - 36, sidebar);
    label(root, frameX + 12, 58, navWidth - 24, "SETTINGS", 11, muted, FontWeight::SemiBold);
    auto& search = inputControl(
        root, frameX + 4, 82, navWidth - 8, 32, "settings:search", m_settingsFields["search"], "Search settings"
    );
    search.setFrameRadius(6);
    search.setHorizontalPadding(10);
    struct NavRow {
      const SettingsPage* page;
      float y;
      bool heading;
    };
    std::vector<NavRow> navRows;
    float navHeight = 0;
    std::string group;
    const auto query = folded(m_settingsFields["search"]);
    for (const auto& entry : pages) {
      if (!matches(entry, query))
        continue;
      if (group != entry.group) {
        group = entry.group;
        navRows.push_back({&entry, navHeight, true});
        navHeight += navHeadingHeight;
      }
      navRows.push_back({&entry, navHeight, false});
      navHeight += navRowHeight;
    }
    const float navH = std::max(30.0F, h - navTop - navBottom);
    m_scrolling.limit(m_settingsNavScroll, std::max(0.0F, navHeight - navH));
    auto nav = std::make_unique<Node>();
    nav->setPosition(frameX, navTop);
    nav->setSize(navWidth, navH);
    nav->setClipChildren(true);
    for (const auto& row : navRows) {
      const auto y = row.y - m_settingsNavScroll;
      if (y < 0 || y + navRowHeight > navH)
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
        item->setFontSize(12);
        item->setVariant(m_settingsPage == row.page->id ? ButtonVariant::TabActive : ButtonVariant::Tab);
        item->setContentAlign(ButtonContentAlign::Start);
        item->setPosition(4, y);
        item->setControlHeight(26);
        item->setRadius(5);
        item->setPadding(0, 8);
        item->setFrameSize(navWidth - 8, 26);
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
          root, frameX + navWidth - 3, navTop + (navH - 32) * m_settingsNavScroll / (navHeight - navH), 3, 32, raised, 2
      );
    settingsButton(root, {frameX + 4, h - 62, navWidth - 8, 32, Action::SignOut, {}}, "Log Out");
    iconButton(root, {w - 56, 52, 30, 30, Action::CloseSettings, {}}, "x", "Close settings (Esc)");
    const auto selected = std::ranges::find_if(pages, [&](const auto& entry) { return m_settingsPage == entry.id; });
    const std::string title = selected == std::end(pages) ? "Settings" : selected->name;
    label(root, contentLeft, 63, contentWidth, title, 22, foreground, FontWeight::Bold);
    const bool unsaved = m_settingsDraft != m_accountSettings && m_settingsLoaded;
    const float saveHeight = unsaved ? (width < 420 ? 82 : 48) : 0;
    const float statusHeight =
        m_settingsStatus.empty() ? 0 : textHeight(m_settingsStatus, 12, FontWeight::Normal, width - 24) + 20;
    const float viewportY = 108,
                viewportH = std::max(
                    40.0F,
                    h - viewportY - 24 - (saveHeight ? saveHeight + 12 : 0) - (statusHeight ? statusHeight + 8 : 0)
                );
    const float previousScroll = m_settingsScroll;
    auto page = std::make_unique<Node>();
    page->setPosition(contentLeft, viewportY);
    page->setSize(contentWidth, viewportH);
    page->setClipChildren(true);
    page->setTextPixelSnap(false);
    auto body = std::make_unique<Node>();
    auto* bodyNode = body.get();
    body->setPosition(14, -m_settingsScroll);
    page->addChild(std::move(body));
    auto& p = *bodyNode;
    const auto firstHit = m_hits.size();
    float cursor = 0, categoryTop = 0;
    RectNode* category = nullptr;
    const auto finishCategory = [&] {
      if (!category)
        return;
      cursor += 8;
      category->setSize(contentWidth, cursor - categoryTop);
      category = nullptr;
    };
    const auto section = [&](const std::string& caption) {
      finishCategory();
      if (cursor)
        cursor += 16;
      categoryTop = cursor;
      auto card = std::make_unique<RectNode>();
      card->setPosition(-14, cursor);
      card->setStyle({.fill = sidebar, .radius = 8});
      card->setZIndex(-1);
      category = card.get();
      p.addChild(std::move(card));
      cursor += caption.empty() ? 4 : 12;
      if (!caption.empty()) {
        cursor += label(p, 0, cursor, width, caption, 12, muted, FontWeight::SemiBold, 0);
        cursor += 8;
      }
    };
    const auto ensureCategory = [&] {
      if (!category)
        section("");
    };
    struct SettingRow {
      float top, height;
    };
    const auto row = [&](const std::string& name, const std::string& description = std::string{}, float trailing = 0,
                         float minimum = 40, bool interactive = false, bool stackControl = false) {
      ensureCategory();
      const float below = stackControl && width < 420 ? 44 : 0;
      const float top = cursor, textWidth = std::max(48.0F, width - (below ? 0 : trailing));
      const float heading = textHeight(name, 13, FontWeight::SemiBold, textWidth);
      const float detail = description.empty() ? 0 : textHeight(description, 12, FontWeight::Normal, textWidth) + 4;
      const float height = std::max(minimum, heading + detail + 20 + below);
      if (interactive && hovered(contentX, viewportY - previousScroll + top, width, height))
        rectangle(p, -6, top, width + 12, height - 1, withAlpha(raised, .45F), 5);
      label(
          p, 0, top + (height - below - heading - detail) / 2, textWidth, name, 13, foreground, FontWeight::SemiBold, 0
      );
      if (!description.empty())
        label(
            p, 0, top + (height - below - heading - detail) / 2 + heading + 4, textWidth, description, 12, muted,
            FontWeight::Normal, 0
        );
      cursor += height;
      return SettingRow{top, height};
    };
    const auto actionRow = [&](const std::string& name, const std::string& description) {
      return row(name, description, 144, 40, false, true);
    };
    const auto rowAction = [&](const SettingRow& item, Action action, const std::string& id, const std::string& caption,
                               bool primary = false) {
      settingsButton(
          p,
          {width < 420 ? 0 : width - 128, width < 420 ? item.top + item.height - 40 : item.top + (item.height - 32) / 2,
           128, 32, action, id},
          caption, primary, false, true, true
      );
    };
    const auto toggle = [&](const std::string& name, bool enabled, const std::string& id,
                            const std::string& description, bool remote = false) {
      const auto item = row(name, description, 62);
      const auto controlId = "toggle:" + id;
      std::unique_ptr<Node> saved;
      if (auto at = m_detachedInputs.find(controlId); at != m_detachedInputs.end()) {
        saved = std::move(at->second);
        m_detachedInputs.erase(at);
      }
      auto control =
          saved ? std::unique_ptr<Toggle>(static_cast<Toggle*>(saved.release())) : std::make_unique<Toggle>();
      m_widgets[controlId] = control.get();
      control->setToggleSize(ToggleSize::Small);
      control->setCheckedImmediate(enabled);
      control->setEnabled(!remote || (m_settingsLoaded && !m_loadingSettings && !m_savingSettings));
      // Small native geometry: 14px thumb + 2px insets + 12px travel.
      control->setPosition(width - 30, item.top + (item.height - 18) / 2);
      control->setFrameSize(30, 18);
      control->setOnChange([this, id, remote](bool) {
        activate({0, 0, 0, 0, remote ? Action::AccountPreference : Action::Preference, id});
        m_window.invalidate();
      });
      p.addChild(std::move(control));
    };
    const auto accountToggle = [&](const std::string& name, const std::string& key, const std::string& description) {
      toggle(name, m_settingsDraft.contains(key) && m_settingsDraft.at(key) != 0, key, description, true);
    };
    const auto choices = [&](const std::string& key, const std::vector<std::string>& names) {
      for (std::size_t i = 0; i < names.size(); ++i) {
        const bool active = m_settingsDraft.contains(key) && m_settingsDraft.at(key) == static_cast<int>(i);
        const auto item = row(names[i], {}, 32, 36, true);
        const float cy = item.top + (item.height - 14) / 2;
        rectangle(p, width - 18, cy, 14, 14, active ? accent : raised, 7);
        rectangle(p, width - 15, cy + 3, 8, 8, active ? ink : background, 4);
        if (m_settingsLoaded && !m_loadingSettings && !m_savingSettings)
          m_hits.push_back(
              {contentX, viewportY - previousScroll + item.top, width, item.height, Action::AccountPreference,
               key + ":" + std::to_string(i)}
          );
      }
    };
    const auto field = [&](const std::string& name, const std::string& key, float height,
                           const std::string& description) {
      ensureCategory();
      cursor += 8;
      cursor += label(p, 0, cursor, width, name, 13, foreground, FontWeight::SemiBold);
      cursor += 4;
      cursor += label(p, 0, cursor, width, description, 12, muted, FontWeight::Normal, 0);
      cursor += 8;
      auto& input =
          inputControl(p, 0, cursor, width, height, "settings:" + key, m_settingsFields[key], {}, key == "bio");
      input.setFrameRadius(6);
      input.setHorizontalPadding(10);
      cursor += height + 16;
    };
    const auto openDiscord = [&](const SettingRow& item) {
      rowAction(item, Action::OpenUrl, "https://discord.com/channels/@me", "Open Discord");
    };
    if (m_settingsPage == "cressida") {
      section("Names & roles");
      toggle(
          "Show server tags in chat", m_preferences.showGuildTags, "guild_tags",
          "Show clickable server badges beside message authors."
      );
      toggle(
          "Show role colors", m_preferences.roleColors, "role_colors", "Use server role colors and gradients for names."
      );
      toggle(
          "Animate gradients on hover", m_preferences.animateRoleColors, "animate_role_colors",
          "Animate name gradients when hovering names, member rows or messages."
      );
      section("Message logger");
      toggle(
          "Keep deleted messages", m_preferences.logDeletedMessages, "log_deleted_messages",
          "Keep deleted messages in chat until you sign out or close Cressida. Disabling this option clears them."
      );
      toggle(
          "Keep edit history", m_preferences.logEditHistory, "log_edit_history",
          "Show earlier message versions until you sign out or close Cressida. Disabling this option clears them."
      );
      section("Expressions");
      toggle(
          "Emoji image links", m_preferences.emojiImageLinks, "emoji_image_links",
          "Send unsupported server emoji as image links. Other clients may show a link or image, depending on channel "
          "embed permissions. Reactions use Discord's normal rules."
      );
    } else if (m_settingsPage == "appearance") {
      section("Theme");
      int index = 0;
      const bool stacked = width < 420;
      const float part = std::min(width, 100.0F);
      const int columns = std::clamp(static_cast<int>((width + 8) / (part + 8)), 1, 3);
      for (const auto& name : {"dark", "light", "midnight"}) {
        settingsButton(
            p,
            {(index % columns) * (part + 8), cursor + (index / columns) * 30, part, 26, Action::Preference,
             "theme:" + std::string(name)},
            name, false, m_preferences.theme == name, true, true
        );
        ++index;
      }
      cursor += ((3 + columns - 1) / columns) * 30 + 8;
      section("Text & interface");
      const auto font = row("Message text size", "Change the size of text in conversations.", 196, 40, false, true);
      const float controlsX = stacked ? 0 : width - 180;
      const float fontY = stacked ? font.top + font.height - 40 : font.top + (font.height - 32) / 2;
      settingsButton(p, {controlsX, fontY, 36, 32, Action::Preference, "font-"}, "−", false, false, true, true);
      label(
          p, controlsX + 50, fontY + 7, 80, std::to_string(static_cast<int>(m_preferences.messageSize)) + " px", 13,
          foreground
      );
      settingsButton(p, {controlsX + 144, fontY, 36, 32, Action::Preference, "font+"}, "+", false, false, true, true);
      const auto zoom =
          row("Interface scale", "Resize the entire interface, including text and controls.", 196, 40, false, true);
      const float zoomY = stacked ? zoom.top + zoom.height - 40 : zoom.top + (zoom.height - 32) / 2;
      settingsButton(p, {controlsX, zoomY, 36, 32, Action::Preference, "zoom-"}, "−", false, false, true, true);
      settingsButton(
          p, {controlsX + 48, zoomY, 84, 32, Action::Preference, "zoom0"},
          std::to_string(static_cast<int>(m_window.uiScale() * 100)) + "%", false, false, true, true
      );
      settingsButton(p, {controlsX + 144, zoomY, 36, 32, Action::Preference, "zoom+"}, "+", false, false, true, true);
      toggle("Compact message spacing", m_preferences.compact, "compact", "Reduce the space between message groups.");
    } else if (m_settingsPage == "text") {
      toggle(
          "Show link previews and embeds", m_preferences.showEmbeds, "embeds",
          "Display preview cards attached to messages."
      );
      toggle(
          "Show image attachments", m_preferences.showImages, "images",
          "Display attached images in chat. Hidden images remain available as links."
      );
      toggle(
          "Automatically play GIFs", m_preferences.autoplayGifs, "autoplay_gifs",
          "Play visible GIFs while this window is focused. When disabled, hover a GIF to play it."
      );
    } else if (m_settingsPage == "account") {
      const auto header = actionRow(m_account.name, "@" + m_account.username);
      rowAction(header, Action::SettingsPage, "profile", "Edit profile", true);
      row("Username", m_account.username);
      row("Account ID", m_account.id);
      const auto signIn = m_demo ? "Demo sign-in is not saved." : m_loginStorageStatus;
      if (!signIn.empty())
        row("Sign-in", signIn);
      openDiscord(
          actionRow("Account security", "Manage your email, password and two-factor authentication in Discord.")
      );
    } else if (m_settingsPage == "profile") {
      if (m_loadingProfile)
        row("Profile", "Loading your profile…");
      field("Display name", "display_name", 36, "The name shown on your profile. Up to 32 characters.");
      field("Pronouns", "pronouns", 36, "Shown alongside your profile information. Up to 40 characters.");
      field("About me", "bio", 96, "Introduce yourself on your profile. Up to 190 characters.");
      settingsButton(
          p, {0, cursor, 132, 32, m_savingProfile || m_loadingProfile ? Action::None : Action::SaveProfile, {}},
          m_savingProfile ? "Saving…" : "Save Changes", true, false, true, true
      );
      settingsButton(
          p, {144, cursor, 76, 32, m_savingProfile ? Action::None : Action::ResetProfile, {}}, "Reset", false, false,
          true, true
      );
      cursor += 40;
    } else if (m_settingsPage == "presence") {
      section("Availability");
      for (const auto& status : {"online", "idle", "dnd", "invisible"}) {
        const auto item =
            row(std::string(presenceName(status)),
                std::string_view(status) == "invisible"
                    ? "Appear offline. Other active Discord sessions may still affect your status."
                    : "",
                44, 40, true);
        const float cy = item.top + (item.height - 12) / 2;
        rectangle(p, width - 14, cy, 12, 12, presenceColor(status), 6);
        if (m_presence.status == status)
          glyph(p, width - 40, cy - 2, 16, "check", accent);
        m_hits.push_back(
            {contentX, viewportY - previousScroll + item.top, width, item.height, Action::SetPresence, status}
        );
      }
      cursor += 12;
      field("Custom status", "custom_status", 36, "Displayed with your availability. Up to 128 characters.");
      settingsButton(p, {0, cursor, 124, 32, Action::SaveCustomStatus, {}}, "Save Status", true, false, true, true);
      cursor += 40;
    } else if (m_settingsPage == "privacy") {
      accountToggle(
          "Block DMs from new server members", "restrict_new_guild_dms",
          "Apply to servers you join from now on. Existing server overrides stay unchanged."
      );
      accountToggle(
          "Disable requests from new server members", "restrict_message_requests",
          "Apply to servers you join from now on. Existing server overrides stay unchanged."
      );
      accountToggle(
          "Contact discovery", "contact_sync", "Allow Discord to use contact syncing to help people find you."
      );
      accountToggle(
          "Hide legacy username badge", "hide_legacy_username",
          "Hide the legacy username badge on your Discord profile."
      );
      section("Profile visibility");
      choices(
          "profile_visibility",
          {"Discord default", "Friends only", "Friends and small servers", "Friends and all servers"}
      );
    } else if (m_settingsPage == "content") {
      section("Direct message spam filter");
      choices("dm_spam_filter", {"Discord default", "Do not filter", "Filter non-friends", "Filter everyone"});
      accountToggle(
          "Conversation safety warnings", "conversation_warnings", "Enable Discord's safety warnings for conversations."
      );
      row("Sensitive media and message requests",
          "Media blurring and message request folders are not available in Cressida yet.");
    } else if (m_settingsPage == "friends") {
      section("Who can send you a friend request");
      const auto flags = m_settingsDraft.contains("friend_sources") ? m_settingsDraft.at("friend_sources") : 14;
      toggle("Everyone", flags & 8, "friend_sources:8", "Allow anyone to send you a friend request.", true);
      toggle(
          "Friends of friends", flags & 2, "friend_sources:2",
          "Allow requests from people who share a friend with you.", true
      );
      toggle(
          "Server members", flags & 4, "friend_sources:4", "Allow requests from people who share a server with you.",
          true
      );
    } else if (m_settingsPage == "notifications") {
      accountToggle(
          "In-app notifications", "in_app_notifications",
          "Sync this notification preference to Discord. Cressida does not deliver desktop notifications or play "
          "notification sounds yet."
      );
      accountToggle(
          "Friends coming online", "friend_online_notifications",
          "Receive notifications in Discord when friends come online."
      );
      accountToggle(
          "Profile update notifications", "profile_notifications",
          "Receive notifications in Discord when friends update their profiles."
      );
      accountToggle(
          "Upcoming server events", "server_event_notifications",
          "Receive reminders in Discord for upcoming server events."
      );
      accountToggle(
          "Notify friends when I stream", "notify_friends_stream",
          "Let Discord notify your friends when you start streaming."
      );
      accountToggle(
          "Notify friends when I update my profile", "notify_friends_profile",
          "Let Discord notify your friends when your profile changes."
      );
      accountToggle(
          "Notify friends when I come online", "notify_friends_online",
          "Let Discord notify your friends when you become available."
      );
      section("Reaction notifications");
      choices("reaction_notifications", {"All conversations", "Direct messages only", "Off"});
    } else if (m_settingsPage == "activity") {
      accountToggle(
          "Share current activity", "show_activity",
          "Share your activity on Discord. Automatic game detection is not available in Cressida yet."
      );
      accountToggle("Show recent games", "recent_games", "Show recently played games on your Discord profile.");
      accountToggle(
          "Allow friends to join activities", "join_friends", "Let friends join activities you share on Discord."
      );
      accountToggle(
          "Allow voice participants to join", "join_voice",
          "Let people in your voice channel join your activities on Discord."
      );
    } else if (listPage(m_settingsPage)) {
      const std::string description = m_settingsPage == "devices"
          ? "Review active sessions here. Sign out other devices in Discord."
          : m_settingsPage == "authorized" ? "Review app access here. Revoke access in Discord."
                                           : "Review linked accounts here. Add or remove connections in Discord.";
      openDiscord(actionRow("Manage in Discord", description));
      if (m_demo)
        row("Account data", "Demo mode does not load account data.");
      else if (m_loadingSettingsLists.contains(m_settingsPage))
        row("Account data", "Loading…");
      else if (m_settingsListErrors.contains(m_settingsPage)) {
        const auto item = actionRow("Could not load account data", m_settingsListErrors[m_settingsPage]);
        rowAction(item, Action::SettingsPage, m_settingsPage, "Retry");
      } else if (m_settingsLists[m_settingsPage].empty())
        row("Account data", "Nothing to show for this account.");
      for (const auto& entry : m_settingsLists[m_settingsPage])
        row(entry.name, entry.detail);
    } else if (m_settingsPage == "keybinds") {
      for (const auto& [key, meaning] :
           {std::pair{"Ctrl + ,", "Open settings"},
            {"Ctrl + + / − / 0", "Zoom in / out / reset"},
            {"Enter / Shift + Enter", "Send message / new line"},
            {"Ctrl + A / C / X / V", "Select all / copy / cut / paste"},
            {"Ctrl + Z / Shift + Ctrl + Z", "Undo / redo"},
            {"Ctrl + ← / →", "Move by word"},
            {"Ctrl + Backspace / Delete", "Delete previous / next word"},
            {"Escape", "Close settings or image viewer"},
            {"Ctrl + Q", "Quit"}}) {
        const float keyWidth = width * .46F;
        const auto item = row(meaning, {}, keyWidth + 16, textHeight(key, 11, FontWeight::Medium, keyWidth) + 20);
        label(p, width - keyWidth, item.top + 10, keyWidth, key, 11, accent, FontWeight::Medium, 0);
      }
      row("Custom shortcuts", "Shortcut remapping is not available yet.");
    } else if (m_settingsPage == "accessibility") {
      const auto item =
          actionRow("Display and text", "Adjust message text size, interface scale and theme in Appearance.");
      rowAction(item, Action::SettingsPage, "appearance", "Appearance");
      row("Accessibility support", "Screen reader announcements and reduced motion preferences are not available yet.");
    } else if (m_settingsPage == "advanced") {
      row("Window decorations", "Cressida follows your desktop's window decoration preference.");
      row("Advanced preferences", "Additional advanced preferences are not available yet.");
    } else {
      const std::string description = m_settingsPage == "voice"
          ? "Calls, microphone selection and audio processing are not available in Cressida yet."
          : m_settingsPage == "subscriptions" ? "Manage Nitro, gifts and payments in Discord."
          : m_settingsPage == "language" ? "Cressida currently uses English. Language selection is not available yet."
          : m_settingsPage == "games" ? "Automatic game detection and manually registered games are not available yet."
                                      : "The in-game overlay is not available yet.";
      openDiscord(actionRow("Not available in Cressida", description));
    }
    finishCategory();
    const float pageHeight = cursor + 8;
    m_scrolling.limit(m_settingsScroll, std::max(0.0F, pageHeight - viewportH));
    bodyNode->setPosition(14, -m_settingsScroll);
    const float correction = previousScroll - m_settingsScroll;
    for (auto i = firstHit; i < m_hits.size(); ++i) {
      auto& hit = m_hits[i];
      hit.y += correction;
      const float end = std::min(viewportY + viewportH, hit.y + hit.height);
      hit.y = std::max(hit.y, viewportY);
      hit.height = std::max(0.0F, end - hit.y);
    }
    root.addChild(std::move(page));
    if (pageHeight > viewportH) {
      const float thumb = std::max(28.0F, viewportH * viewportH / pageHeight);
      rectangle(
          root, contentX + width + 10, viewportY + (viewportH - thumb) * m_settingsScroll / (pageHeight - viewportH), 3,
          thumb, raised, 2
      );
    }
    if (unsaved) {
      const float y = h - 24 - saveHeight - (statusHeight ? statusHeight + 8 : 0);
      rectangle(root, contentX, y, width, saveHeight, panel, 7);
      label(root, contentX + 12, y + 14, width < 420 ? width - 24 : width - 236, "Unsaved changes", 12, foreground);
      const float buttonY = y + saveHeight - 40, saveWidth = std::min(124.0F, width * .55F),
                  resetWidth = std::min(76.0F, width * .35F);
      settingsButton(
          root,
          {contentX + width - saveWidth - resetWidth - 24,
           buttonY,
           resetWidth,
           32,
           m_savingSettings ? Action::None : Action::ResetAccountSettings,
           {}},
          "Reset", false, false, true, true
      );
      settingsButton(
          root,
          {contentX + width - saveWidth - 12,
           buttonY,
           saveWidth,
           32,
           m_savingSettings ? Action::None : Action::SaveAccountSettings,
           {}},
          m_savingSettings ? "Saving…" : "Save Changes", true, false, true, true
      );
    }
    if (statusHeight) {
      rectangle(root, contentX, h - 24 - statusHeight, width, statusHeight, panel, 7);
      label(root, contentX + 12, h - 14 - statusHeight, width - 24, m_settingsStatus, 12, muted, FontWeight::Normal, 0);
    }
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
