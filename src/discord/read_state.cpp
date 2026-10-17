#include "discord/read_state.h"

#include "discord/models.h"

#include <nlohmann/json.hpp>

namespace discord {
  namespace {
    std::string string(const nlohmann::json& value, const char* key) {
      const auto at = value.find(key);
      return at != value.end() && at->is_string() ? at->get<std::string>() : "";
    }
    bool newer(const std::string& a, const std::string& b) {
      return a.size() != b.size() ? a.size() > b.size() : a > b;
    }
  } // namespace
  void ReadState::channels(const nlohmann::json& value, const std::string& guild) {
    if (!value.is_array())
      return;
    for (const auto& channel : value) {
      if (!channel.is_object())
        continue;
      const auto id = string(channel, "id");
      if (!validSnowflake(id) || m_channels.size() >= 100000)
        continue;
      auto& entry = m_channels[id];
      entry.guild = guild;
      const auto latest = string(channel, "last_message_id");
      if (validSnowflake(latest) && newer(latest, entry.latest))
        entry.latest = latest;
    }
  }
  void ReadState::ready(const nlohmann::json& value) {
    m_channels.clear();
    if (value.contains("private_channels"))
      channels(value["private_channels"]);
    if (value.contains("guilds") && value["guilds"].is_array())
      for (const auto& guild : value["guilds"])
        if (guild.contains("channels"))
          channels(guild["channels"], string(guild, "id"));
    if (!value.contains("read_state"))
      return;
    const auto& state = value["read_state"];
    const auto& entries = state.is_object() && state.contains("entries") ? state["entries"] : state;
    if (!entries.is_array())
      return;
    for (const auto& item : entries) {
      if (!item.is_object() || item.value("read_state_type", 0) != 0)
        continue;
      const auto id = string(item, "id");
      if (!validSnowflake(id) || m_channels.size() >= 100000)
        continue;
      auto& entry = m_channels[id];
      entry.acknowledged = string(item, "last_message_id");
      entry.mentions = item.value("mention_count", 0);
    }
  }
  void ReadState::message(const nlohmann::json& value, const std::string& self) {
    const auto id = string(value, "channel_id"), latest = string(value, "id");
    if (!validSnowflake(id) || !validSnowflake(latest) || m_channels.size() >= 100000)
      return;
    auto& entry = m_channels[id];
    if (value.contains("guild_id"))
      entry.guild = string(value, "guild_id");
    if (newer(latest, entry.latest))
      entry.latest = latest;
    if (value.contains("author") && string(value["author"], "id") == self) {
      entry.acknowledged = latest;
      entry.mentions = 0;
    }
  }
  void ReadState::acknowledge(const nlohmann::json& value) {
    if (value.value("ack_type", 0) != 0)
      return;
    const auto id = string(value, "channel_id"), message = string(value, "message_id");
    if (!validSnowflake(id) || !validSnowflake(message) || m_channels.size() >= 100000)
      return;
    auto& entry = m_channels[id];
    if (value.value("manual", false) || newer(message, entry.acknowledged) || entry.acknowledged == message) {
      entry.acknowledged = message;
      entry.mentions = value.value("mention_count", 0);
    }
  }
  std::map<std::string, std::string> ReadState::unread() const {
    std::map<std::string, std::string> result;
    for (const auto& [id, entry] : m_channels)
      if (entry.mentions > 0 || (!entry.latest.empty() && newer(entry.latest, entry.acknowledged)))
        result[id] = entry.guild;
    return result;
  }
} // namespace discord
