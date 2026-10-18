#include "app/client_application.h"
#include "app/font_catalog.h"
#include "app/theme.h"
#include "render/scene/input_area.h"
#include "render/scene/rect_node.h"
#include "ui/controls/button.h"
#include "ui/controls/scrollbar.h"
#include "ui/controls/toggle.h"

#include <algorithm>
#include <cmath>
#include <iostream>
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
        {"themes", "Themes", "CRESSIDA", "theme palette catppuccin rose pine gruvbox custom toml colors dark light"},
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
        {"appearance", "Appearance", "APP SETTINGS", "font family text size theme scale compact"},
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
    m_fontPickerOpen = false;
    m_fontPickerBounds = {};
    m_scrolling.stop(m_fontPickerScroll);
    m_fontPickerScroll = 0;
    m_installedFonts.clear();
    m_fontMatches.clear();
    m_settingsFields.erase("font_search");
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
  bool ClientApplication::fontPreference(const std::string& id) {
    if (!m_settingsOpen || m_settingsPage != "appearance")
      return false;
    if (id == "font-picker") {
      m_fontPickerOpen = !m_fontPickerOpen;
      m_settingsFields["font_search"].clear();
      m_scrolling.stop(m_fontPickerScroll);
      m_fontPickerScroll = 0;
      m_fontPickerBounds = {};
      if (m_fontPickerOpen) {
        m_fontPickerReturnScroll = m_settingsScroll;
        m_installedFonts = installedFontFamilies();
        m_fontMatches = m_installedFonts;
        m_fontFilter.clear();
        m_window.context().notifyFontConfigChanged();
        m_focusedInput = "settings:font_search";
      } else {
        m_controlInput.cancelPointerCapture();
        m_controlInput.setFocus(nullptr);
        m_focusedInput.clear();
        m_scrolling.stop(m_settingsScroll);
        m_settingsScroll = m_fontPickerReturnScroll;
      }
    } else if (id.starts_with("font-family:")) {
      if (!m_fontPickerOpen)
        return true;
      const auto family = id.substr(12);
      if (!family.empty() && std::ranges::find(m_installedFonts, family) == m_installedFonts.end())
        return true;
      m_preferences.fontFamily = family;
      m_fontPickerOpen = false;
      m_fontPickerBounds = {};
      m_scrolling.stop(m_fontPickerScroll);
      m_controlInput.cancelPointerCapture();
      m_settingsFields["font_search"].clear();
      m_controlInput.setFocus(nullptr);
      m_focusedInput.clear();
      m_scrolling.stop(m_settingsScroll);
      m_settingsScroll = m_fontPickerReturnScroll;
      savePreferences();
    } else
      return false;
    m_window.invalidate();
    return true;
  }
  bool ClientApplication::fontPickerClick(float x, float y) {
    if (!m_settingsOpen || !m_fontPickerOpen)
      return false;
    const auto& bounds = m_fontPickerBounds;
    if (!bounds.contains(x, y)) {
      fontPreference("font-picker");
      return true;
    }
    const bool search = x >= bounds.x + 8 && x < bounds.x + bounds.width - 8 && y >= bounds.y + 8 && y < bounds.y + 40;
    const auto bar = m_widgets.find("font-picker-scrollbar");
    const bool scrollbar = x >= bounds.x + bounds.width - 16
        && y >= bounds.y + 80
        && bar != m_widgets.end()
        && static_cast<Scrollbar*>(bar->second)->visible();
    if (search || scrollbar) {
      m_controlInput.pointerButton(x, y + m_window.titlebarHeight() - 36, 272, true, 0, 0, false);
      m_window.invalidate();
      return true;
    }
    for (auto at = m_hits.rbegin(); at != m_hits.rend(); ++at)
      if (at->action == Action::Preference && at->id.starts_with("font-family:") && at->contains(x, y)) {
        const auto id = at->id;
        fontPreference(id);
        break;
      }
    return true;
  }
  void ClientApplication::drawFontPicker(Node& root, float w, float h) {
    if (!m_fontPickerOpen || m_settingsPage != "appearance")
      return;
    const auto& query = m_settingsFields["font_search"];
    if (query != m_fontFilter) {
      m_fontFilter = query;
      m_fontMatches.clear();
      for (const auto& name : m_installedFonts)
        if (fontFamilyMatches(name, query))
          m_fontMatches.push_back(name);
    }
    const float width = std::min(420.0F, w - 32);
    const float x = std::clamp(m_fontPickerAnchor.x + m_fontPickerAnchor.width - width, 16.0F, w - width - 16);
    const float below = h - m_fontPickerAnchor.y - m_fontPickerAnchor.height - 20;
    const float above = m_fontPickerAnchor.y - 56;
    const bool down = below >= std::min(320.0F, above);
    const float full = std::max<std::size_t>(1, m_fontMatches.size()) * 30;
    const float viewport = std::min(full, std::max(30.0F, std::min(600.0F, (down ? below : above) - 88)));
    const float height = viewport + 88;
    const float y = std::clamp(
        down ? m_fontPickerAnchor.y + m_fontPickerAnchor.height + 6 : m_fontPickerAnchor.y - height - 6, 40.0F,
        std::max(40.0F, h - height - 12)
    );
    m_fontPickerBounds = {x, y, width, height, Action::None, {}};
    m_hits.push_back({0, 36, w, h - 36, Action::None, {}});
    m_hits.push_back(m_fontPickerBounds);
    rectangle(root, x, y, width, height, raised, 8);
    rectangle(root, x + 1, y + 1, width - 2, height - 2, sidebar, 7);
    inputControl(root, x + 8, y + 8, width - 16, 32, "settings:font_search", query, "Search installed fonts");
    const auto option = [&](Node& parent, float localX, float localY, float rowWidth, const std::string& name,
                            const std::string& value, float hitY, float hitHeight) {
      const bool selected = m_preferences.fontFamily == value;
      const bool over = hitHeight > 0 && hovered(x + 8, hitY, rowWidth, hitHeight);
      if (selected || over)
        rectangle(parent, localX, localY, rowWidth, 26, selected ? lerpColor(sidebar, accent, .16F) : raised, 5);
      const auto metrics = m_window.target().renderer().measureText(name, 12, FontWeight::Normal);
      label(
          parent, localX + 8, localY + (26 - (metrics.bottom - metrics.top)) / 2, rowWidth - 16, name, 12, foreground
      );
      if (hitHeight > 0)
        m_hits.push_back({x + 8, hitY, rowWidth, hitHeight, Action::Preference, "font-family:" + value});
    };
    // Reset remains pinned while results scroll, including for an empty search.
    option(root, x + 8, y + 46, width - 16, "System default", {}, y + 46, 26);
    auto list = std::make_unique<Node>();
    list->setPosition(x + 8, y + 80);
    list->setSize(width - 28, viewport);
    list->setClipChildren(true);
    list->setTextPixelSnap(false);
    m_scrolling.limit(m_fontPickerScroll, std::max(0.0F, full - viewport));
    // Keep 25 rows prepared around the viewport; font metadata is inexpensive,
    // and the remaining families need no scene nodes or glyphs until approached.
    const std::size_t visible = static_cast<std::size_t>(std::ceil(viewport / 30)) + 1;
    const std::size_t overscan = visible < 25 ? (25 - visible) / 2 : 0;
    const auto top = static_cast<std::size_t>(m_fontPickerScroll / 30);
    const auto first =
        std::min(top > overscan ? top - overscan : 0, m_fontMatches.size() > 25 ? m_fontMatches.size() - 25 : 0);
    const auto end = std::min(m_fontMatches.size(), first + 25);
    for (auto i = first; i < end; ++i) {
      const float rowY = i * 30 - m_fontPickerScroll + 2;
      const float hitTop = std::max(0.0F, rowY), hitBottom = std::min(viewport, rowY + 26);
      option(
          *list, 0, rowY, width - 28, m_fontMatches[i], m_fontMatches[i], y + 80 + hitTop,
          std::max(0.0F, hitBottom - hitTop)
      );
    }
    if (m_fontMatches.empty())
      label(
          *list, 8, 6, width - 44, m_installedFonts.empty() ? "No installed fonts found" : "No matching fonts", 12,
          muted
      );
    root.addChild(std::move(list));
    settingsScrollbar(root, "font-picker-scrollbar", {x + width - 14, y + 80, 8, viewport}, full, m_fontPickerScroll);
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
    settingsScrollbar(
        root, "settings-nav-scrollbar", {frameX + navWidth - 4, navTop, 8, navH}, navHeight, m_settingsNavScroll
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
    } else if (m_settingsPage == "themes") {
      section("Color themes");
      row("Choose a theme", "Each theme includes dark and light colors. Choose the mode in Appearance.");
      if (std::ranges::find(m_themeSnapshot.themes, m_preferences.colorTheme, &ColorTheme::id)
          == m_themeSnapshot.themes.end())
        row("Theme unavailable", "Using Cressida until " + m_preferences.colorTheme + " is available again.");
      cursor += 6;
      const int columns = width >= 480 ? 2 : 1;
      const float cardWidth = (width - (columns - 1) * 12) / columns;
      constexpr float cardHeight = 184, cardStep = cardHeight + 12;
      std::size_t index = 0;
      for (const auto& value : m_themeSnapshot.themes) {
        const float x = (index % columns) * (cardWidth + 12), y = cursor + (index / columns) * cardStep;
        const bool selected = m_preferences.colorTheme == value.id;
        auto tile = std::make_unique<RectNode>();
        tile->setPosition(x, y);
        tile->setSize(cardWidth, cardHeight);
        tile->setStyle(
            {.fill = panel, .border = selected ? accent : withAlpha(muted, .18F), .radius = 7, .borderWidth = 1}
        );
        auto* card = p.addChild(std::move(tile));
        label(*card, 12, 10, cardWidth - 24, value.name, 13, foreground, FontWeight::SemiBold);
        label(*card, 12, 30, cardWidth - 24, value.description, 11, muted);
        const float previewWidth = (cardWidth - 30) / 2;
        for (const auto light : {false, true}) {
          const auto& colors = light ? value.light : value.dark;
          const float px = 10 + (light ? previewWidth + 10 : 0);
          rectangle(*card, px, 51, previewWidth, 91, rgbHex(colors.background), 5);
          rectangle(*card, px, 51, 28, 91, rgbHex(colors.sidebar), 3);
          rectangle(*card, px, 51, 7, 91, rgbHex(colors.rail), 3);
          rectangle(*card, px + 11, 61, 12, 4, rgbHex(colors.muted), 1);
          rectangle(*card, px + 10, 76, 14, 10, rgbHex(colors.accent), 2);
          rectangle(*card, px + 13, 79, 8, 3, rgbHex(colors.ink), 1);
          rectangle(*card, px + 11, 93, 12, 3, rgbHex(colors.muted), 1);
          rectangle(*card, px + 11, 103, 9, 3, rgbHex(colors.muted), 1);
          const float chatX = px + 35, chatWidth = previewWidth - 43;
          label(*card, chatX, 57, chatWidth, light ? "Light" : "Dark", 10, rgbHex(colors.muted));
          rectangle(*card, chatX, 77, 12, 12, rgbHex(colors.function), 6);
          rectangle(*card, chatX + 9, 85, 4, 4, rgbHex(colors.success), 2);
          rectangle(*card, chatX + 18, 77, chatWidth * .55F, 4, rgbHex(colors.accent), 1);
          label(*card, chatX + 18, 84, chatWidth - 18, "Hello!", 10, rgbHex(colors.foreground));
          rectangle(*card, chatX, 104, chatWidth, 15, rgbHex(colors.panel), 2);
          float codeX = chatX + 5;
          const float codeUnit = (chatWidth - 16) / 7;
          for (const auto color : {colors.keyword, colors.function, colors.string}) {
            rectangle(*card, codeX, 110, codeUnit * 2, 3, rgbHex(color), 1);
            codeX += codeUnit * 2 + 3;
          }
          rectangle(*card, chatX, 125, chatWidth, 10, rgbHex(colors.raised), 2);
          rectangle(*card, chatX + 5, 129, chatWidth * .5F, 2, rgbHex(colors.muted), 1);
        }
        label(*card, 12, 158, cardWidth - 122, value.bundled ? "Built in" : "Custom", 10, muted);
        m_hits.push_back(
            {contentX + x, viewportY - previousScroll + y, cardWidth, cardHeight, Action::Preference,
             "palette:" + value.id}
        );
        settingsButton(
            *card, {cardWidth - 108, 148, 96, 26, Action::Preference, "palette:" + value.id},
            selected ? "Selected" : "Use theme", selected, false, true, true
        );
        ++index;
      }
      cursor += ((index + columns - 1) / columns) * cardStep;
      section("Custom themes");
      const auto directory = userThemeDirectory();
      const auto custom = actionRow(
          "Theme files",
          directory.empty()
              ? "The theme folder is unavailable. Set a home or configuration directory."
              : "Save the template as a .toml file in " + directory.string() + ". Changes reload automatically."
      );
      rowAction(custom, Action::Preference, "theme-template", "Copy template");
      if (!m_themeSnapshot.issues.empty()) {
        section("Theme file errors");
        for (const auto& issue : m_themeSnapshot.issues)
          row(issue.file, issue.message + " The last valid version stays active when available.");
      }
    } else if (m_settingsPage == "appearance") {
      section("Color mode");
      int index = 0;
      const bool stacked = width < 420;
      const float part = std::min(width, 100.0F);
      const int columns = std::clamp(static_cast<int>((width + 8) / (part + 8)), 1, 3);
      for (const auto& name : {"dark", "light", "system"}) {
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
      const auto family = actionRow("Font family", "Choose an installed font for the interface and messages.");
      rowAction(
          family, Action::Preference, "font-picker",
          m_preferences.fontFamily.empty() ? "System default" : m_preferences.fontFamily
      );
      m_fontPickerAnchor = m_hits.back();
      const auto font = row("Message text size", "Change the size of text in conversations.", 196, 40, false, true);
      const float controlsX = stacked ? 0 : width - 180;
      const float fontY = stacked ? font.top + font.height - 40 : font.top + (font.height - 32) / 2;
      settingsButton(p, {controlsX, fontY, 36, 32, Action::Preference, "font-"}, "−", false, false, true, true);
      const auto fontSize = std::to_string(static_cast<int>(m_preferences.messageSize)) + " px";
      const auto fontMetrics = m_window.target().renderer().measureText(fontSize, 13, FontWeight::Normal);
      label(
          p, controlsX + 90 - fontMetrics.width / 2, fontY + (32 - (fontMetrics.bottom - fontMetrics.top)) / 2,
          fontMetrics.width + 1, fontSize, 13, foreground
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
    m_fontPickerAnchor.y += correction;
    root.addChild(std::move(page));
    settingsScrollbar(
        root, "settings-scrollbar", {contentX + width + 4, viewportY, 8, viewportH}, pageHeight, m_settingsScroll
    );
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
    drawFontPicker(root, w, h);
  }
  void ClientApplication::demoFontStep() {
    const auto require = [](bool ok, const char* message) {
      if (!ok)
        throw std::runtime_error(message);
    };
    const auto click = [&](const std::string& id) {
      const auto at = std::ranges::find_if(m_hits, [&](const auto& hit) {
        return hit.action == Action::Preference && hit.id == id && hit.height > 0;
      });
      require(at != m_hits.end(), "Font setting action must be visible");
      const auto hit = *at;
      m_window.click(hit.x + hit.width / 2, hit.y + hit.height / 2 + m_window.titlebarHeight() - 36, 272);
      m_window.released();
    };
    const auto drag = [&](const std::string& id) {
      const auto* bar = static_cast<Scrollbar*>(m_widgets.at(id));
      require(bar->visible(), "Overflowing settings lists require a native scrollbar");
      const auto* thumb = bar->children()[1].get();
      const float x = bar->x() + bar->width() / 2;
      const float y = bar->y() + thumb->y() + thumb->height() / 2 + m_window.titlebarHeight() - 36;
      m_window.motion(x, y);
      m_window.click(x, y, 272);
      m_window.motion(x, y + 40);
      require(m_controlInput.pointerCaptured(), "Settings scrollbar captures the pointer while dragging");
    };
    const auto step = m_demoInteractionStep++;
    if (step == 0) {
      click("font-picker");
      require(m_installedFonts.size() > 25, "Font replay needs enough installed families to exercise virtualization");
    } else if (step == 1) {
      require(m_fontPickerOpen && m_inputs.contains("settings:font_search"), "Picker retains a native search field");
      require(m_fontPickerBounds.height <= 688, "Font choices stay inside a bounded picker");
      require(
          std::ranges::count_if(
              m_hits,
              [](const auto& hit) { return hit.action == Action::Preference && hit.id.starts_with("font-family:"); }
          ) <= 26,
          "Font picker only builds a bounded option window"
      );
      drag("font-picker-scrollbar");
    } else if (step == 2) {
      require(
          m_fontPickerScroll > 0 && m_settingsScroll == m_fontPickerReturnScroll,
          "Font scrollbar moves the picker independently of the settings page"
      );
      require(m_controlInput.pointerCaptured(), "Font scrollbar capture survives scene reconstruction");
      m_window.motion(m_pointerX, m_window.height() * 2);
      m_window.released();
      focusControl("settings:font_search");
      pasteControl("settings:font_search", m_installedFonts.front());
      require(!m_profileDirty, "Font search must not dirty account profile settings");
    } else if (step == 3) {
      require(
          m_settingsFields["font_search"] == m_installedFonts.front() && m_fontPickerScroll == 0,
          "Native font search updates the filter and resets its scrollbar"
      );
      click("font-family:" + m_installedFonts.front());
    } else if (step == 4) {
      require(!m_fontPickerOpen && m_preferences.fontFamily == m_installedFonts.front(), "Font selection applies");
      auto& renderer = m_window.target().renderer();
      const auto normal = renderer.measureText("Font preview Wi", 14);
      const auto chosen = renderer.measureText(
          "Font preview Wi", 14, FontWeight::Normal, 0, 0, TextAlign::Start, m_preferences.fontFamily
      );
      require(std::abs(normal.width - chosen.width) < .01F, "Selected font drives default text measurements");
      click("font-picker");
      m_settingsFields["font_search"] = "no-font-family-cressida-987654321";
    } else if (step == 5) {
      require(
          std::ranges::none_of(
              m_hits,
              [](const auto& hit) {
                return hit.action == Action::Preference
                    && hit.id.starts_with("font-family:")
                    && hit.id != "font-family:";
              }
          ),
          "Unmatched font search must hide installed family choices"
      );
      click("font-family:");
    } else if (step == 6) {
      require(m_preferences.fontFamily.empty() && !m_fontPickerOpen, "System default clears the saved family");
      const auto before = m_window.target().renderer().measureText("Font preview Wi", 14);
      const auto system = m_window.target().renderer().measureText(
          "Font preview Wi", 14, FontWeight::Normal, 0, 0, TextAlign::Start, "sans-serif"
      );
      require(std::abs(before.width - system.width) < .01F, "Reset restores system default measurements");
      click("font-picker");
    } else if (step == 7) {
      settingsKey({.symbol = XKB_KEY_Escape});
      require(!m_fontPickerOpen && m_settingsOpen, "Escape closes the font picker before settings");
      drag("settings-nav-scrollbar");
    } else if (step == 8) {
      require(
          m_settingsNavScroll > 0 && m_controlInput.pointerCaptured(),
          "Navigation scrollbar drags and retains capture across scene reconstruction"
      );
      m_window.motion(m_pointerX, m_window.height() * 2);
      m_window.released();
      selectSettingsPage("notifications");
      m_window.setUiScale(1.8F);
    } else if (step == 9) {
      drag("settings-scrollbar");
    } else if (step == 10) {
      require(
          m_settingsScroll > 0 && m_controlInput.pointerCaptured(),
          "Settings page scrollbar drags and retains capture across scene reconstruction"
      );
      m_window.motion(m_pointerX, m_window.height() * 2);
      m_window.released();
      selectSettingsPage("cressida");
    } else if (step == 11) {
      require(!m_fontPickerOpen && m_installedFonts.empty(), "Page navigation clears the font catalog");
    }
    std::clog << "Offline font settings replay step " << step + 1 << " passed\n";
    m_window.invalidate();
  }
  void ClientApplication::demoThemeStep() {
    const auto require = [](bool ok, const char* message) {
      if (!ok)
        throw std::runtime_error(message);
    };
    const auto click = [&](const std::string& id) {
      const auto found = std::ranges::find_if(m_hits, [&](const auto& hit) {
        return hit.action == Action::Preference && hit.id == id && hit.height > 0;
      });
      require(found != m_hits.end(), "Theme control must be visible");
      const auto hit = *found;
      m_window.click(hit.x + hit.width / 2, hit.y + hit.height / 2 + m_window.titlebarHeight() - 36, 272);
      m_window.released();
    };
    const auto step = m_demoInteractionStep++;
    if (step == 0) {
      require(m_themeSnapshot.themes.size() >= 4, "Bundled TOML themes must be available offline");
      click("palette:catppuccin");
    } else if (step == 1) {
      require(
          m_preferences.colorTheme == "catppuccin" && background == rgbHex(0x1E1E2E),
          "Selecting Catppuccin applies Mocha"
      );
      selectSettingsPage("appearance");
    } else if (step == 2) {
      require(
          std::ranges::count_if(
              m_hits, [](const auto& hit) { return hit.action == Action::Preference && hit.id.starts_with("theme:"); }
          ) == 3,
          "Appearance offers exactly dark, light and system modes"
      );
      click("theme:light");
    } else if (step == 3) {
      require(
          m_preferences.colorTheme == "catppuccin" && lightTheme && background == rgbHex(0xEFF1F5),
          "Light mode applies Latte without changing the selected theme"
      );
      selectSettingsPage("themes");
    } else if (step == 4) {
      click("palette:rose-pine");
    } else if (step == 5) {
      require(lightTheme && background == rgbHex(0xFAF4ED), "Rosé Pine uses Dawn in light mode");
      selectSettingsPage("appearance");
      activate({0, 0, 0, 0, Action::Preference, "theme:system"});
      m_themeSnapshot.systemLight = false;
    } else if (step == 6) {
      require(!lightTheme && background == rgbHex(0x191724), "System dark uses the same theme's dark variant");
      m_themeSnapshot.systemLight = true;
    } else if (step == 7) {
      require(lightTheme && background == rgbHex(0xFAF4ED), "A desktop preference change switches the active variant");
      selectSettingsPage("themes");
      activate({0, 0, 0, 0, Action::Preference, "palette:../invalid"});
      require(m_preferences.colorTheme == "rose-pine", "Unknown palette IDs cannot replace the selection");
    } else if (step == 8) {
      click("palette:gruvbox");
    } else if (step == 9) {
      require(lightTheme && background == rgbHex(0xFBF1C7), "Gruvbox uses its warm light palette");
      m_themeSnapshot.systemLight = false;
    } else if (step == 10) {
      require(!lightTheme && background == rgbHex(0x282828), "Gruvbox also follows system dark mode");
    }
    std::clog << "Offline theme settings replay step " << step + 1 << " passed\n";
    m_window.invalidate();
  }
  void ClientApplication::settingsKey(const Key& key) {
    if (key.symbol == XKB_KEY_Escape) {
      if (m_fontPickerOpen) {
        fontPreference("font-picker");
        return;
      }
      m_settingsOpen = false;
      m_settingsField.clear();
      m_controlInput.setFocus(nullptr);
      m_focusedInput.clear();
      m_window.invalidate();
    }
  }
} // namespace app
