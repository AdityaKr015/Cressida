#include "app/client_application.h"
#include "app/theme.h"
#include "core/files/resource_paths.h"
#include "render/core/texture_manager.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace app {
  using namespace theme;
  void ClientApplication::prepareEmojiPicker() {
    m_emojiCatalog.load(paths::assetPath("emoji.json"), paths::assetPath("emoji-aliases.json"));
    if (m_demo) {
      if (m_loadedEmojiGuilds.empty() && !m_guilds.empty()) {
        for (std::size_t g = 0; g < m_guilds.size(); ++g) {
          std::vector<discord::Emoji> emojis;
          for (int i = 0; i < 16; ++i) {
            discord::Emoji emoji;
            emoji.id = std::to_string(800000000000000000ULL + g * 100 + i);
            emoji.name = i == 0 ? "umbriel" : "project_" + std::to_string(i);
            emoji.guild = m_guilds[g].id;
            emoji.animated = i % 3 == 0;
            auto animation = std::make_shared<media::Animation>();
            animation->width = animation->height = 48;
            for (int f = 0; f < 2; ++f) {
              media::Frame frame;
              frame.duration = std::chrono::milliseconds(150);
              frame.rgba.resize(48 * 48 * 4);
              for (int y = 0; y < 48; ++y)
                for (int x = 0; x < 48; ++x) {
                  const auto at = (y * 48 + x) * 4;
                  if ((x - 24) * (x - 24) + (y - 24) * (y - 24) < 380) {
                    frame.rgba[at] = 110 + i * 7;
                    frame.rgba[at + 1] = 110 + f * 50;
                    frame.rgba[at + 2] = 240;
                    frame.rgba[at + 3] = 255;
                  }
                }
              animation->frames.push_back(std::move(frame));
            }
            animation->duration = std::chrono::milliseconds(300);
            const auto path = discord::emojiPath(emoji.id, emoji.animated);
            auto& cached = m_images[path];
            cached.texture = m_window.context().textureManager().loadFromRgba(animation->frames[0].rgba.data(), 48, 48);
            if (emoji.animated)
              cached.animation = animation;
            emojis.push_back(std::move(emoji));
          }
          m_emojiCatalog.setGuild(m_guilds[g].id, std::move(emojis));
          m_loadedEmojiGuilds.insert(m_guilds[g].id);
        }
        m_emojiPreferences.favorites = {"heart", "800000000000000000"};
        m_emojiPreferences.frequent = {"joy", "thumbsup", "fire", "800000000000000001"};
        m_emojiPreferencesLoaded = true;
      }
      return;
    }
    if (!m_emojiPreferencesLoaded && !m_loadingEmojiPreferences && m_emojiError.empty()) {
      m_loadingEmojiPreferences = m_client.emojiPreferences();
      if (!m_loadingEmojiPreferences)
        m_emojiError = "Could not load emoji favorites. Try again.";
    }
    const auto guild = discord::validSnowflake(m_emojiSection) ? m_emojiSection : m_guildId;
    if (!guild.empty()
        && !m_loadedEmojiGuilds.contains(guild)
        && !m_loadingEmojiGuilds.contains(guild)
        && !m_emojiGuildErrors.contains(guild)) {
      if (m_client.guildEmojis(guild))
        m_loadingEmojiGuilds.insert(guild);
      else
        m_emojiGuildErrors[guild] = "Could not load server emoji.";
    }
  }
  void ClientApplication::closeEmojiPicker() {
    m_emojiMenu = m_emojiOpened = false;
    m_scrolling.stop(m_emojiScroll);
    m_scrolling.stop(m_emojiRailScroll);
    if (m_focusedInput == "emoji-search") {
      m_controlInput.setFocus(nullptr);
      m_focusedInput.clear();
    }
  }
  void ClientApplication::clearEmojiState() {
    closeEmojiPicker();
    m_emojiCatalog.clearGuilds();
    m_emojiPreferences = {};
    m_recentEmojis.clear();
    m_loadedEmojiGuilds.clear();
    m_loadingEmojiGuilds.clear();
    m_emojiGuildErrors.clear();
    m_pendingEmojiFavorites.clear();
    m_emojiPreferencesLoaded = m_loadingEmojiPreferences = m_emojiPreferencesDirty = false;
    m_emojiError.clear();
    m_emojiQuery.clear();
    m_emojiSection = "all";
    m_emojiJump.clear();
    m_emojiScroll = m_emojiRailScroll = 0;
  }
  void ClientApplication::emojiEvent(discord::Event& event) {
    if (event.operation == discord::Operation::GuildEmojis) {
      m_loadingEmojiGuilds.erase(event.resource);
      if (event.error.empty()) {
        m_emojiGuildErrors.erase(event.resource);
        m_loadedEmojiGuilds.insert(event.resource);
        m_emojiCatalog.setGuild(event.resource, std::move(event.emojis));
      } else
        m_emojiGuildErrors[event.resource] = event.error;
      return;
    }
    if (event.resource == "changed")
      m_emojiPreferencesDirty = true;
    else {
      if (event.operation == discord::Operation::SaveEmojiFavorite)
        m_pendingEmojiFavorites.erase(event.resource);
      else
        m_loadingEmojiPreferences = false;
      m_emojiError = event.error;
      if (event.error.empty() && event.emojiPreferences) {
        m_emojiPreferences = std::move(*event.emojiPreferences);
        m_emojiPreferencesLoaded = true;
      }
    }
    if (event.error.empty()
        && m_emojiPreferencesDirty
        && m_emojiPreferencesLoaded
        && !m_loadingEmojiPreferences
        && m_pendingEmojiFavorites.empty()) {
      m_loadingEmojiPreferences = m_client.emojiPreferences();
      if (m_loadingEmojiPreferences)
        m_emojiPreferencesDirty = false;
    }
  }
  std::string ClientApplication::favoriteEmojiKey(const discord::Emoji& emoji) const {
    for (const auto& key : m_emojiPreferences.favorites) {
      const auto* item = m_emojiCatalog.find(key);
      if (key == emoji.key() || (item && item->key() == emoji.key()))
        return key;
    }
    return {};
  }
  void ClientApplication::favoriteEmoji(const std::string& key) {
    const auto* emoji = m_emojiCatalog.find(key);
    if (!emoji)
      return;
    const auto favorite = favoriteEmojiKey(*emoji);
    const auto storedKey = favorite.empty() ? emoji->key() : favorite;
    if (m_pendingEmojiFavorites.contains(storedKey))
      return;
    if (m_demo) {
      if (favorite.empty())
        m_emojiPreferences.favorites.insert(m_emojiPreferences.favorites.begin(), storedKey);
      else
        std::erase(m_emojiPreferences.favorites, storedKey);
    } else if (m_client.saveEmojiFavorite(storedKey, favorite.empty())) {
      m_pendingEmojiFavorites.insert(storedKey);
      m_emojiError.clear();
    } else
      m_emojiError = "Could not update emoji favorites. Try again.";
  }
  void ClientApplication::pickEmoji(const std::string& key) {
    const auto* emoji = m_emojiCatalog.find(key);
    if (!emoji || (!emoji->available && (!m_reactionMessage.empty() || !m_preferences.fakeNitroEmojis)))
      return;
    if (m_reactionMessage.empty()
        && !m_preferences.fakeNitroEmojis
        && discord::emojiNeedsImage(*emoji, m_dmMode ? "" : m_guildId, m_account.premiumType.value_or(0)))
      return;
    const auto picked = *emoji;
    if (!m_reactionMessage.empty()) {
      startInteraction(
          {.kind = discord::InteractionKind::AddReaction,
           .channel = m_reactionChannel,
           .message = m_reactionMessage,
           .emoji = picked.reaction()}
      );
      m_reactionMessage.clear();
      m_reactionChannel.clear();
    } else {
      closeEmojiPicker();
      focusControl("composer");
      if (!m_demo)
        pasteControl("composer", picked.token());
    }
    if (!m_demo) {
      m_sessionStore.recordEmoji(m_account.id, picked.key());
      m_recentEmojis = m_sessionStore.frequentEmojis(m_account.id);
    } else {
      std::erase(m_recentEmojis, picked.key());
      m_recentEmojis.insert(m_recentEmojis.begin(), picked.key());
    }
    closeEmojiPicker();
  }
  void ClientApplication::drawEmojiPicker(Node& root, float w, float h) {
    if (!m_emojiOpened) {
      m_emojiOpened = true;
      m_controlInput.setFocus(nullptr);
      m_focusedInput = "emoji-search";
    }
    prepareEmojiPicker();
    const float width = std::min(552.0F, w - 32), height = std::min(520.0F, std::max(250.0F, m_composerTop - 64));
    const float x =
        std::clamp(w - (m_showMembers && w >= 1000 ? 240 : 0) - width - 24, 16.0F, std::max(16.0F, w - width - 16));
    const float y = std::max(44.0F, m_composerTop - height - 8);
    m_emojiBounds = {x, y, width, height, Action::None, {}};
    m_hits.push_back({0, 36, w, h - 36, Action::CloseEmoji, {}});
    rectangle(root, x, y, width, height, sidebar, 12);
    m_hits.push_back(m_emojiBounds);
    if (m_reactionMessage.empty())
      button(root, {x + 12, y + 10, 66, 30, Action::ToggleGif, {}}, "GIFs");
    label(
        root, x + (m_reactionMessage.empty() ? 92 : 16), y + 17, width - 150,
        m_reactionMessage.empty() ? "Emoji" : "Add reaction", 16, foreground, FontWeight::SemiBold
    );
    iconButton(root, {x + width - 44, y + 9, 32, 32, Action::CloseEmoji, {}}, "x", "Close emoji picker");
    inputControl(root, x + 12, y + 52, width - 24, 38, "emoji-search", m_emojiQuery, "Search emoji");
    const float top = y + 104, available = height - 158, gridX = x + 64, gridWidth = width - 76;
    auto rail = std::make_unique<Node>();
    rail->setPosition(x + 8, top);
    rail->setSize(44, available);
    rail->setClipChildren(true);
    float railY = 0;
    auto railItem = [&](const std::string& id, const std::string& icon, const std::string& title,
                        const discord::Guild* guild = nullptr) {
      const float ry = railY - m_emojiRailScroll;
      railY += 44;
      if (ry + 36 <= 0 || ry >= available)
        return;
      const float clippedTop = std::max(0.0F, ry), clippedBottom = std::min(available, ry + 36);
      if (guild) {
        if (m_emojiSection == id || hovered(x + 8, top + ry, 36, 36))
          rectangle(*rail, 0, ry, 36, 36, raised, 9);
        avatar(*rail, 4, ry + 4, 28, guild->icon, guild->name);
        m_hits.push_back({x + 8, top + clippedTop, 36, clippedBottom - clippedTop, Action::EmojiSection, id});
        if (hovered(x + 8, top + clippedTop, 36, clippedBottom - clippedTop)) {
          m_tooltip = title;
          m_tooltipX = x + 24;
          m_tooltipY = top + ry;
        }
      } else {
        iconButton(*rail, {0, ry, 36, 36, Action::EmojiSection, id}, icon, title, m_emojiSection == id);
        m_hits.back().y = top + clippedTop;
        m_hits.back().height = clippedBottom - clippedTop;
      }
    };
    railItem("favorites", "star", "Favorites");
    railItem("recent", "clock", "Frequently used");
    railItem("all", "mood-smile", "All emoji");
    std::vector<const discord::Guild*> guilds;
    for (const auto& folder : m_folders)
      for (const auto& id : folder.guildIds) {
        const auto guild = std::ranges::find(m_guilds, id, &discord::Guild::id);
        if (guild != m_guilds.end())
          guilds.push_back(&*guild);
      }
    for (const auto& guild : m_guilds)
      if (std::ranges::find(guilds, &guild) == guilds.end())
        guilds.push_back(&guild);
    for (const auto* guild : guilds)
      railItem(guild->id, "", guild->name, guild);
    const std::vector<std::pair<std::string, std::string>> categories = {{"people", "mood-smile"},
                                                                         {"animals", "paw"},
                                                                         {"nature", "leaf"},
                                                                         {"food", "apple"},
                                                                         {"activity", "ball-football"},
                                                                         {"travel", "plane"},
                                                                         {"objects", "bulb"},
                                                                         {"symbols", "hash"},
                                                                         {"flags", "flag"}};
    for (const auto& [category, icon] : categories)
      railItem("unicode:" + category, icon, category);
    m_scrolling.limit(m_emojiRailScroll, std::max(0.0F, railY - available));
    rail->setTextPixelSnap(false);
    root.addChild(std::move(rail));
    struct Group {
      std::string id, title;
      std::vector<const discord::Emoji*> items;
    };
    std::vector<Group> groups;
    auto appendKeys = [&](std::string id, std::string title, const std::vector<std::string>& keys) {
      Group group{std::move(id), std::move(title), {}};
      std::set<std::string> seen;
      for (const auto& key : keys)
        if (const auto* emoji = m_emojiCatalog.find(key); emoji && seen.insert(emoji->key()).second)
          group.items.push_back(emoji);
      groups.push_back(std::move(group));
    };
    appendKeys("favorites", "Favorites", m_emojiPreferences.favorites);
    auto recent = m_recentEmojis;
    recent.insert(recent.end(), m_emojiPreferences.frequent.begin(), m_emojiPreferences.frequent.end());
    for (const auto* key : {"heart", "+1", "joy", "fire", "sob", "eyes", "tada", "100"})
      recent.emplace_back(key);
    if (recent.size() > 64)
      recent.resize(64);
    appendKeys("recent", "Frequently used", recent);
    for (const auto* guild : guilds)
      if (!m_emojiCatalog.guild(guild->id).empty() || guild->id == m_emojiSection)
        appendKeys(guild->id, guild->name, m_emojiCatalog.guild(guild->id));
    for (const auto& [category, _] : categories) {
      appendKeys("unicode:" + category, category, m_emojiCatalog.category(category));
    }
    auto lower = [](std::string text) {
      for (auto& c : text)
        c = std::tolower(static_cast<unsigned char>(c));
      return text;
    };
    auto query = lower(m_emojiQuery);
    std::erase(query, ':');
    auto matches = [&](const discord::Emoji& emoji) {
      if (query.empty() || lower(emoji.name).contains(query) || emoji.unicode == query)
        return true;
      for (const auto& word : emoji.aliases)
        if (lower(word).contains(query))
          return true;
      for (const auto& word : emoji.keywords)
        if (lower(word).contains(query))
          return true;
      return false;
    };
    const int columns = std::max(1, int(gridWidth / 48));
    const float cell = gridWidth / columns;
    float cursor = 0;
    std::map<std::string, float> offsets;
    for (auto& group : groups) {
      std::erase_if(group.items, [&](const auto* emoji) { return !matches(*emoji); });
      if (!query.empty() && group.items.empty())
        continue;
      offsets[group.id] = cursor;
      cursor += 32 + std::max(1, int((group.items.size() + columns - 1) / columns)) * 48 + 12;
    }
    m_scrolling.limit(m_emojiScroll, std::max(0.0F, cursor - available));
    if (!m_emojiJump.empty() && offsets.contains(m_emojiJump)) {
      m_scrolling.stop(m_emojiScroll);
      m_emojiScroll = std::min(offsets[m_emojiJump], std::max(0.0F, cursor - available));
      m_emojiJump.clear();
    }
    auto grid = std::make_unique<Node>();
    grid->setPosition(gridX, top);
    grid->setSize(gridWidth, available);
    grid->setClipChildren(true);
    grid->setTextPixelSnap(false);
    std::map<std::string, std::string> favorites;
    for (const auto& key : m_emojiPreferences.favorites)
      if (const auto* emoji = m_emojiCatalog.find(key))
        favorites.try_emplace(emoji->key(), key);
    m_emojiHovered.clear();
    for (const auto& group : groups) {
      if (!offsets.contains(group.id))
        continue;
      const float gy = offsets[group.id] - m_emojiScroll;
      if (gy + 32 > 0 && gy < available)
        label(*grid, 0, gy + 6, gridWidth, group.title, 12, muted, FontWeight::SemiBold);
      if (group.items.empty() && gy + 32 >= 0 && gy + 32 < available) {
        const bool loading =
            m_loadingEmojiGuilds.contains(group.id) || (group.id == "favorites" && m_loadingEmojiPreferences);
        const bool error = m_emojiGuildErrors.contains(group.id);
        label(
            *grid, 4, gy + 36, gridWidth - 44,
            error                         ? m_emojiGuildErrors[group.id]
                : loading                 ? "Loading…"
                : group.id == "favorites" ? "Star an emoji to keep it here."
                                          : "No emoji to show.",
            12, muted
        );
        if (error && gy + 62 < available)
          iconButton(
              *grid, {gridWidth - 32, gy + 32, 28, 28, Action::EmojiRetry, group.id}, "reload", "Retry server emoji"
          );
      }
      for (std::size_t i = 0; i < group.items.size(); ++i) {
        const auto& emoji = *group.items[i];
        const float ex = (i % columns) * cell, ey = gy + 32 + (i / columns) * 48;
        if (ey + 44 <= 0 || ey >= available)
          continue;
        const float clippedTop = std::max(0.0F, ey), clippedBottom = std::min(available, ey + 44);
        const bool over = hovered(gridX + ex, top + clippedTop, cell, clippedBottom - clippedTop);
        if (over) {
          rectangle(*grid, ex, ey, cell, 44, raised, 7);
          m_emojiHovered = emoji.key();
        }
        if (emoji.id.empty())
          label(*grid, ex + (cell - 36) / 2, ey + 4, 40, emoji.unicode, 30, emoji.available ? foreground : muted);
        else if (!image(*grid, ex + (cell - 36) / 2, ey + 4, 36, discord::emojiPath(emoji.id, emoji.animated), 0))
          glyph(*grid, ex + 12, ey + 10, 20, "loader", muted);
        const bool selectable = m_reactionMessage.empty() ? m_preferences.fakeNitroEmojis
                || !discord::emojiNeedsImage(emoji, m_dmMode ? "" : m_guildId, m_account.premiumType.value_or(0))
                                                          : emoji.available;
        if (selectable)
          m_hits.push_back(
              {gridX + ex, top + clippedTop, cell, clippedBottom - clippedTop, Action::PickEmoji, emoji.key()}
          );
        else
          glyph(*grid, ex + 3, ey + 25, 14, "lock", muted);
        const auto foundFavorite = favorites.find(emoji.key());
        const auto favorite = foundFavorite == favorites.end() ? std::string{} : foundFavorite->second;
        if (over || !favorite.empty()) {
          const float sx = ex + cell - 17, sy = ey;
          if (over)
            rectangle(*grid, sx, sy, 17, 17, sidebar, 4);
          glyph(
              *grid, sx + 1, sy + 1, 14, favorite.empty() ? "star" : "star-filled", favorite.empty() ? muted : accent
          );
          const float starTop = std::max(0.0F, sy), starBottom = std::min(available, sy + 17);
          if (starBottom > starTop && !m_pendingEmojiFavorites.contains(favorite.empty() ? emoji.key() : favorite))
            m_hits.push_back({gridX + sx, top + starTop, 17, starBottom - starTop, Action::FavoriteEmoji, emoji.key()});
        }
      }
    }
    root.addChild(std::move(grid));
    if (!m_emojiError.empty()) {
      label(root, gridX, y + height - 45, gridWidth - 80, m_emojiError, 11, muted, FontWeight::Normal, 2);
      iconButton(
          root, {x + width - 44, y + height - 44, 28, 28, Action::EmojiRetry, {}}, "reload", "Retry emoji favorites"
      );
    } else if (const auto* emoji = m_emojiCatalog.find(m_emojiHovered)) {
      if (emoji->id.empty())
        label(root, gridX, y + height - 43, 36, emoji->unicode, 27, foreground);
      else
        image(root, gridX, y + height - 43, 32, discord::emojiPath(emoji->id, emoji->animated), 0);
      label(
          root, gridX + 44, y + height - 44, gridWidth - 48, ":" + emoji->name + ":", 13, foreground,
          FontWeight::SemiBold
      );
      const auto guild = std::ranges::find(m_guilds, emoji->guild, &discord::Guild::id);
      auto source = emoji->guild.empty() ? std::string("Unicode emoji")
          : guild == m_guilds.end()      ? "Server emoji"
                                         : "from " + guild->name;
      if (m_reactionMessage.empty()
          && discord::emojiNeedsImage(*emoji, m_dmMode ? "" : m_guildId, m_account.premiumType.value_or(0)))
        source =
            m_preferences.fakeNitroEmojis ? "Sends as an image link" : "Enable FakeNitro in Cressida → Expressions";
      label(root, gridX + 44, y + height - 24, gridWidth - 48, source, 11, muted);
    }
  }
} // namespace app
