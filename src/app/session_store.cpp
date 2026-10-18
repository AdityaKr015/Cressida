#include "app/session_store.h"

#include "api/actions.h"
#include "api/emoji.h"
#include "api/models.h"
#include "app/font_catalog.h"
#include "app/theme_catalog.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <unistd.h>

namespace app {
  namespace {
    nlohmann::json read(const std::filesystem::path& path) {
      std::ifstream file(path, std::ios::binary);
      std::string bytes(65537, '\0');
      file.read(bytes.data(), bytes.size());
      bytes.resize(file.gcount());
      if (bytes.size() > 65536)
        return nlohmann::json::object();
      auto value = nlohmann::json::parse(bytes, nullptr, false);
      return value.is_object() ? value : nlohmann::json::object();
    }
    nlohmann::json readState(const std::filesystem::path& path, const std::filesystem::path& legacy) {
      // The new file is authoritative, even when empty or damaged. Only fall
      // back before the first save; keep the previous installation's file intact.
      if (legacy.empty() || std::filesystem::exists(path))
        return read(path);
      return read(legacy);
    }
    void writeState(const std::filesystem::path& path, const nlohmann::json& value) {
      const auto bytes = value.dump();
      std::filesystem::create_directories(path.parent_path());
      std::string temporary = (path.parent_path() / ".session-XXXXXX").string();
      const int fd = mkstemp(temporary.data());
      if (fd < 0)
        return;
      fchmod(fd, 0600);
      std::size_t written = 0;
      while (written < bytes.size()) {
        const auto count = write(fd, bytes.data() + written, bytes.size() - written);
        if (count <= 0)
          break;
        written += count;
      }
      close(fd);
      if (written == bytes.size()) {
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (!error)
          return;
      }
      unlink(temporary.c_str());
    }
  } // namespace
  SessionStore::SessionStore(std::filesystem::path path) : m_path(std::move(path)) {
    if (!m_path.empty())
      return;
    if (const auto* state = std::getenv("XDG_STATE_HOME"); state && *state) {
      m_path = std::filesystem::path(state) / "cressida/session.json";
      m_legacyPath = std::filesystem::path(state) / "discord-cpp/session.json";
    } else if (const auto* home = std::getenv("HOME"); home && *home) {
      m_path = std::filesystem::path(home) / ".local/state/cressida/session.json";
      m_legacyPath = std::filesystem::path(home) / ".local/state/discord-cpp/session.json";
    }
  }
  std::optional<LastChannel> SessionStore::load(const std::string& account) const {
    try {
      const auto all = readState(m_path, m_legacyPath);
      if (!all.contains(account) || !all[account].is_object())
        return std::nullopt;
      const auto& value = all[account];
      LastChannel result{value.value("guild", ""), value.value("channel", ""), value.value("scale", 1.0F)};
      if ((!result.guild.empty() && !api::validSnowflake(result.guild)) || !api::validSnowflake(result.channel))
        return std::nullopt;
      result.scale = std::clamp(result.scale, 0.67F, 1.5F);
      return result;
    } catch (...) {
      return std::nullopt;
    }
  }
  void SessionStore::save(const std::string& account, const LastChannel& channel) const {
    if (m_path.empty() || !api::validSnowflake(account) || !api::validSnowflake(channel.channel))
      return;
    try {
      auto value = readState(m_path, m_legacyPath);
      value[account]["guild"] = channel.guild;
      value[account]["channel"] = channel.channel;
      value[account]["scale"] = channel.scale;
      value[account]["channels"][channel.guild.empty() ? "@me" : channel.guild] = channel.channel;
      writeState(m_path, value);
    } catch (...) { /* UI preference failures do not interrupt the account session. */
    }
  }
  std::string SessionStore::channelForGuild(const std::string& account, const std::string& guild) const {
    try {
      const auto all = readState(m_path, m_legacyPath);
      if (!all.contains(account) || !all[account].is_object())
        return {};
      const auto& value = all[account];
      const auto key = guild.empty() ? "@me" : guild;
      if (value.contains("channels") && value["channels"].is_object()) {
        auto channel = value["channels"].value(key, std::string{});
        if (api::validSnowflake(channel))
          return channel;
      }
      // Migrate the previous single-selection format without losing its last channel.
      if (value.value("guild", std::string{}) == guild) {
        auto channel = value.value("channel", std::string{});
        if (api::validSnowflake(channel))
          return channel;
      }
    } catch (...) {
    }
    return {};
  }
  Preferences SessionStore::preferences(const std::string& account) const {
    Preferences result;
    try {
      const auto all = readState(m_path, m_legacyPath);
      if (!all.contains(account) || !all[account].contains("preferences"))
        return result;
      const auto& value = all[account]["preferences"];
      result.theme = value.value("theme", "dark");
      if (!validColorMode(result.theme))
        result.theme = "dark";
      result.colorTheme = value.value("color_theme", "cressida");
      if (!validThemeId(result.colorTheme))
        result.colorTheme = "cressida";
      result.messageSize = std::clamp(value.value("message_size", 14.0F), 12.0F, 20.0F);
      result.fontFamily = value.value("font_family", std::string{});
      if (!validFontFamily(result.fontFamily))
        result.fontFamily.clear();
      result.compact = value.value("compact", false);
      result.showEmbeds = value.value("embeds", true);
      result.showImages = value.value("images", true);
      result.autoplayGifs = value.value("autoplay_gifs", true);
      result.emojiImageLinks = value.value("emoji_image_links", false);
      result.logDeletedMessages = value.value("log_deleted_messages", false);
      result.logEditHistory = value.value("log_edit_history", false);
      result.roleColors = value.value("role_colors", true);
      result.animateRoleColors = value.value("animate_role_colors", true);
      result.showGuildTags = value.value("guild_tags", true);
      const float width = value.value("channel_sidebar_width", 248.0F);
      result.channelSidebarWidth = std::isfinite(width) ? std::clamp(width, 200.0F, 480.0F) : 248.0F;
    } catch (...) {
      return {};
    }
    return result;
  }
  void SessionStore::savePreferences(const std::string& account, const Preferences& preferences) const {
    if (m_path.empty() || !api::validSnowflake(account))
      return;
    try {
      auto all = readState(m_path, m_legacyPath);
      all[account]["preferences"] = {
          {"theme", validColorMode(preferences.theme) ? preferences.theme : "dark"},
          {"color_theme", validThemeId(preferences.colorTheme) ? preferences.colorTheme : "cressida"},
          {"message_size", preferences.messageSize},
          {"font_family", validFontFamily(preferences.fontFamily) ? preferences.fontFamily : std::string{}},
          {"compact", preferences.compact},
          {"embeds", preferences.showEmbeds},
          {"images", preferences.showImages},
          {"autoplay_gifs", preferences.autoplayGifs},
          {"emoji_image_links", preferences.emojiImageLinks},
          {"log_deleted_messages", preferences.logDeletedMessages},
          {"log_edit_history", preferences.logEditHistory},
          {"role_colors", preferences.roleColors},
          {"animate_role_colors", preferences.animateRoleColors},
          {"guild_tags", preferences.showGuildTags},
          {"channel_sidebar_width",
           std::isfinite(preferences.channelSidebarWidth) ? std::clamp(preferences.channelSidebarWidth, 200.0F, 480.0F)
                                                          : 248.0F}
      };
      writeState(m_path, all);
    } catch (...) {
    }
  }
  std::vector<std::string> SessionStore::frequentEmojis(const std::string& account) const {
    std::vector<std::pair<int, std::string>> ranked;
    try {
      const auto all = readState(m_path, m_legacyPath);
      if (all.contains(account) && all[account].contains("emoji_usage") && all[account]["emoji_usage"].is_object())
        for (const auto& [key, count] : all[account]["emoji_usage"].items()) {
          if (ranked.size() >= 64)
            break;
          if (api::validEmojiKey(key) && count.is_number_integer() && count > 0 && count <= 1000000)
            ranked.emplace_back(count.get<int>(), key);
        }
    } catch (...) {
    }
    std::ranges::sort(ranked, [](const auto& a, const auto& b) {
      return a.first > b.first || (a.first == b.first && a.second < b.second);
    });
    std::vector<std::string> result;
    for (const auto& [_, key] : ranked)
      result.push_back(key);
    return result;
  }
  void SessionStore::recordEmoji(const std::string& account, const std::string& key) const {
    if (m_path.empty() || !api::validSnowflake(account) || !api::validEmojiKey(key))
      return;
    try {
      auto all = readState(m_path, m_legacyPath);
      auto& usage = all[account]["emoji_usage"];
      if (!usage.is_object())
        usage = nlohmann::json::object();
      if (!usage.contains(key) && usage.size() >= 64) {
        auto lowest = usage.begin();
        for (auto it = usage.begin(); it != usage.end(); ++it)
          if (!it.value().is_number_integer() || (lowest.value().is_number_integer() && it.value() < lowest.value()))
            lowest = it;
        usage.erase(lowest);
      }
      const auto previous =
          usage.contains(key) && usage[key].is_number_integer() ? std::clamp(usage[key].get<int>(), 0, 999999) : 0;
      usage[key] = previous + 1;
      writeState(m_path, all);
    } catch (...) {
    }
  }
  std::vector<std::string> SessionStore::frequentReactions(const std::string& account) const {
    std::vector<std::pair<int, std::string>> ranked;
    try {
      const auto all = readState(m_path, m_legacyPath);
      if (all.contains(account) && all[account].contains("reaction_usage")) {
        const auto& usage = all[account]["reaction_usage"];
        if (usage.is_object())
          for (const auto& [emoji, count] : usage.items()) {
            if (ranked.size() == 64)
              break;
            if (count.is_number_integer()
                && count > 0
                && count <= 1000000
                && api::interactionRequest({.channel = "1", .message = "1", .emoji = emoji}))
              ranked.emplace_back(count.get<int>(), emoji);
          }
      }
    } catch (...) {
    }
    std::ranges::sort(ranked, [](const auto& a, const auto& b) {
      return a.first > b.first || (a.first == b.first && a.second < b.second);
    });
    std::vector<std::string> result;
    for (const auto& [_, emoji] : ranked)
      result.push_back(emoji);
    for (const auto* emoji : {"❤️", "👍", "😂", "🔥"})
      if (std::ranges::find(result, emoji) == result.end())
        result.emplace_back(emoji);
    return result;
  }
  void SessionStore::recordReaction(const std::string& account, const std::string& emoji) const {
    if (m_path.empty()
        || !api::validSnowflake(account)
        || !api::interactionRequest({.channel = "1", .message = "1", .emoji = emoji}))
      return;
    try {
      auto all = readState(m_path, m_legacyPath);
      auto& usage = all[account]["reaction_usage"];
      if (!usage.is_object())
        usage = nlohmann::json::object();
      if (!usage.contains(emoji) && usage.size() >= 64) {
        // Retain popular entries when a new reaction joins the bounded history.
        auto lowest = usage.begin();
        for (auto it = usage.begin(); it != usage.end(); ++it)
          if (!it.value().is_number_integer() || (lowest.value().is_number_integer() && it.value() < lowest.value()))
            lowest = it;
        usage.erase(lowest);
      }
      const auto previous = usage.contains(emoji) && usage[emoji].is_number_integer()
          ? std::clamp(usage[emoji].get<int>(), 0, 999999)
          : 0;
      usage[emoji] = previous + 1;
      writeState(m_path, all);
    } catch (...) {
    }
  }
} // namespace app
