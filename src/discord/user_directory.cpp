#include "discord/user_directory.h"

#include <nlohmann/json.hpp>

namespace discord {
  namespace {
    constexpr std::size_t maxUsers = 100000;
    std::string text(const nlohmann::json& value, const char* key) {
      const auto field = value.find(key);
      return field != value.end() && field->is_string() ? field->get<std::string>() : std::string{};
    }
  } // namespace

  void UserDirectory::apply(const UserUpdates& update) {
    if (update.reset) {
      m_names.clear();
      m_statuses.clear();
      m_relationships.clear();
      m_mutedChannels.clear();
      m_tags.clear();
    }
    for (const auto& [id, user] : update.users) {
      if (!validSnowflake(id))
        continue;
      if (user.nickname) {
        if (user.nickname->empty())
          m_names.erase(id);
        else if (m_names.contains(id) || m_names.size() < maxUsers)
          m_names[id] = *user.nickname;
      }
      if (user.status && (m_statuses.contains(id) || m_statuses.size() < maxUsers))
        m_statuses[id] = *user.status;
      if (user.relationship && (m_relationships.contains(id) || m_relationships.size() < maxUsers))
        m_relationships[id] = *user.relationship;
      if (user.guildTag && (m_tags.contains(id) || m_tags.size() < maxUsers))
        m_tags[id] = *user.guildTag;
    }
    for (const auto& [id, muted] : update.mutedChannels)
      if (validSnowflake(id) && (m_mutedChannels.contains(id) || m_mutedChannels.size() < maxUsers))
        m_mutedChannels[id] = muted;
  }

  std::string UserDirectory::name(const std::string& user, const std::string& fallback) const {
    const auto found = m_names.find(user);
    return found == m_names.end() ? fallback : found->second;
  }
  std::string UserDirectory::status(const std::string& user, const std::string& fallback) const {
    const auto found = m_statuses.find(user);
    return found == m_statuses.end() ? fallback : found->second;
  }
  int UserDirectory::relationship(const std::string& user) const {
    const auto found = m_relationships.find(user);
    return found == m_relationships.end() ? 0 : found->second;
  }
  bool UserDirectory::muted(const std::string& channel) const {
    const auto found = m_mutedChannels.find(channel);
    return found != m_mutedChannels.end() && found->second;
  }
  std::optional<GuildTag> UserDirectory::guildTag(const std::string& user, const std::optional<GuildTag>& fallback) const {
    const auto at = m_tags.find(user);
    return at == m_tags.end() ? fallback : at->second;
  }

  UserUpdates UserDirectory::consume(std::string_view type, const nlohmann::json& data) {
    UserUpdates changes;
    if (!data.is_object())
      return changes;
    if (type == "READY") {
      changes.reset = true;
      m_globalStatuses.clear();
      m_guildStatuses.clear();
    }
    auto recordTag = [&](const nlohmann::json& user) {
      const auto id = text(user, "id");
      const auto at = user.find(user.contains("primary_guild") ? "primary_guild" : "clan");
      if (!validSnowflake(id) || at == user.end() || (changes.users.size() >= maxUsers && !changes.users.contains(id)))
        return;
      if (at->is_null() || (at->is_object() && at->contains("identity_enabled") && (*at)["identity_enabled"] == false))
        changes.users[id].guildTag.emplace(std::nullopt);
      else if (const auto tag = parseGuildTag(user))
        changes.users[id].guildTag.emplace(tag);
    };
    if (type == "USER_UPDATE") recordTag(data);
    if (data.contains("user")) recordTag(data["user"]);
    if (data.contains("author")) recordTag(data["author"]);
    if ((type == "READY" || type == "READY_SUPPLEMENTAL") && data.contains("users") && data["users"].is_array())
      for (const auto& user : data["users"]) recordTag(user);
    auto relationship = [&](const nlohmann::json& item, bool removed = false) {
      if (!item.is_object())
        return;
      if (item.contains("user")) recordTag(item["user"]);
      auto id = text(item, "id");
      if (!validSnowflake(id) || changes.users.size() >= maxUsers)
        return;
      if (removed)
        changes.users[id].relationship = 0;
      else if (item.contains("type") && item["type"].is_number_integer())
        changes.users[id].relationship = item["type"].get<int>();
      const auto nickname = item.find("nickname");
      if (removed || (nickname != item.end() && nickname->is_null()))
        changes.users[id].nickname = "";
      else if (nickname != item.end() && nickname->is_string() && nickname->get_ref<const std::string&>().size() <= 512)
        changes.users[id].nickname = nickname->get<std::string>();
    };
    auto presence = [&](const nlohmann::json& item, const std::string& guild = std::string{}) {
      if (!item.is_object())
        return;
      if (item.contains("user")) recordTag(item["user"]);
      auto id = text(item, "user_id");
      if (const auto user = item.find("user"); user != item.end() && user->is_object())
        id = text(*user, "id");
      auto status = text(item, "status");
      if (!validSnowflake(id)
          || (status != "online"
              && status != "idle"
              && status != "dnd"
              && status != "offline"
              && status != "invisible"))
        return;
      if (status == "invisible")
        status = "offline";
      const auto scope = text(item, "guild_id");
      const auto& guildId = scope.empty() ? guild : scope;
      if (guildId.empty()) {
        if (!m_globalStatuses.contains(id) && m_globalStatuses.size() >= maxUsers)
          return;
        m_globalStatuses[id] = status;
      } else {
        if (!validSnowflake(guildId) || (!m_guildStatuses.contains(id) && m_guildStatuses.size() >= maxUsers))
          return;
        auto& guilds = m_guildStatuses[id];
        if (!guilds.contains(guildId) && guilds.size() >= 1000)
          return;
        guilds[guildId] = status;
        // A global friend presence is authoritative for DMs. Guild-scoped data
        // is useful for non-friends, but must not override that global status.
        if (m_globalStatuses.contains(id))
          status = m_globalStatuses.at(id);
        else {
          status = "offline";
          for (const auto& [key, value] : guilds)
            if (value != "offline") {
              status = value;
              if (value == "online")
                break;
            }
        }
      }
      if (changes.users.size() < maxUsers || changes.users.contains(id))
        changes.users[id].status = status;
    };
    auto notificationSettings = [&](const nlohmann::json& settings) {
      if (!settings.is_object() || !settings.contains("channel_overrides") || !settings["channel_overrides"].is_array())
        return;
      for (const auto& channel : settings["channel_overrides"]) {
        const auto id = text(channel, "channel_id");
        if (validSnowflake(id)
            && channel.contains("muted")
            && channel["muted"].is_boolean()
            && changes.mutedChannels.size() < maxUsers) {
          bool muted = channel["muted"].get<bool>();
          // Expiring mutes are reconciled by subsequent settings updates; an
          // indefinite mute from this client uses end_time=null.
          changes.mutedChannels[id] = muted;
        }
      }
    };
    if (type == "READY" || type == "READY_SUPPLEMENTAL") {
      if (const auto settings = data.find("user_guild_settings"); settings != data.end()) {
        const auto* entries = &*settings;
        if (entries->is_object() && entries->contains("entries"))
          entries = &(*entries)["entries"];
        if (entries->is_array())
          for (const auto& item : *entries)
            notificationSettings(item);
      }
      if (const auto relationships = data.find("relationships"); relationships != data.end()) {
        const auto* entries = &*relationships;
        if (entries->is_object() && entries->contains("entries"))
          entries = &(*entries)["entries"];
        if (entries->is_array())
          for (const auto& item : *entries)
            relationship(item);
      }
      if (const auto guilds = data.find("guilds"); guilds != data.end() && guilds->is_array())
        for (const auto& guild : *guilds)
          if (const auto list = guild.find("presences"); list != guild.end() && list->is_array())
            for (const auto& item : *list)
              presence(item, text(guild, "id"));
      if (const auto merged = data.find("merged_presences"); merged != data.end() && merged->is_object()) {
        if (const auto guilds = merged->find("guilds"); guilds != merged->end() && guilds->is_array())
          for (std::size_t i = 0; i < guilds->size(); ++i) {
            std::string guildId;
            if (data.contains("guilds") && data["guilds"].is_array() && i < data["guilds"].size())
              guildId = text(data["guilds"][i], "id");
            if ((*guilds)[i].is_array())
              for (const auto& item : (*guilds)[i])
                presence(item, guildId);
          }
        if (const auto friends = merged->find("friends"); friends != merged->end() && friends->is_array())
          for (const auto& item : *friends)
            presence(item);
      }
      if (const auto list = data.find("presences"); list != data.end() && list->is_array())
        for (const auto& item : *list)
          presence(item);
    } else if (type == "RELATIONSHIP_ADD" || type == "RELATIONSHIP_UPDATE" || type == "RELATIONSHIP_REMOVE")
      relationship(data, type == "RELATIONSHIP_REMOVE");
    else if (type == "PRESENCE_UPDATE")
      presence(data);
    else if (type == "USER_GUILD_SETTINGS_UPDATE")
      notificationSettings(data);
    apply(changes);
    return changes;
  }
} // namespace discord
