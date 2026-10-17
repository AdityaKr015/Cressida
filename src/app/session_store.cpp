#include "app/session_store.h"

#include "discord/models.h"

#include <algorithm>
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
      if ((!result.guild.empty() && !discord::validSnowflake(result.guild)) || !discord::validSnowflake(result.channel))
        return std::nullopt;
      result.scale = std::clamp(result.scale, 0.67F, 1.5F);
      return result;
    } catch (...) {
      return std::nullopt;
    }
  }
  void SessionStore::save(const std::string& account, const LastChannel& channel) const {
    if (m_path.empty() || !discord::validSnowflake(account) || !discord::validSnowflake(channel.channel))
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
        if (discord::validSnowflake(channel))
          return channel;
      }
      // Migrate the previous single-selection format without losing its last channel.
      if (value.value("guild", std::string{}) == guild) {
        auto channel = value.value("channel", std::string{});
        if (discord::validSnowflake(channel))
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
      if (result.theme != "dark" && result.theme != "light" && result.theme != "midnight")
        result.theme = "dark";
      result.messageSize = std::clamp(value.value("message_size", 14.0F), 12.0F, 20.0F);
      result.compact = value.value("compact", false);
      result.showEmbeds = value.value("embeds", true);
      result.showImages = value.value("images", true);
    } catch (...) {
      return {};
    }
    return result;
  }
  void SessionStore::savePreferences(const std::string& account, const Preferences& preferences) const {
    if (m_path.empty() || !discord::validSnowflake(account))
      return;
    try {
      auto all = readState(m_path, m_legacyPath);
      all[account]["preferences"] = {
          {"theme", preferences.theme},
          {"message_size", preferences.messageSize},
          {"compact", preferences.compact},
          {"embeds", preferences.showEmbeds},
          {"images", preferences.showImages}
      };
      writeState(m_path, all);
    } catch (...) {
    }
  }
} // namespace app
