#pragma once

#include "api/models.h"

#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <utility>

namespace api {
  // User-account participant snapshots are separate from guild member ranges.
  // https://github.com/dolfies/discord.py-self/blob/master/discord/state.py
  inline std::optional<std::vector<MemberRow>>
  threadMemberList(const nlohmann::json& data, const std::string& guild, const std::string& thread) {
    if (!validSnowflake(guild) || !validSnowflake(thread) || !data.is_object())
      return {};
    const auto guildId = data.find("guild_id"), threadId = data.find("thread_id"), members = data.find("members");
    if (guildId == data.end()
        || !guildId->is_string()
        || *guildId != guild
        || threadId == data.end()
        || !threadId->is_string()
        || *threadId != thread
        || members == data.end()
        || !members->is_array()
        || members->size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
      return {};
    std::vector<MemberRow> rows;
    std::set<std::string> seen;
    for (const auto& entry : *members) {
      if (!entry.is_object())
        return {};
      const auto id = entry.find("user_id"), member = entry.find("member");
      if (id == entry.end()
          || !id->is_string()
          || !validSnowflake(id->get<std::string>())
          || member == entry.end()
          || !member->is_object())
        return {};
      const auto user = member->find("user");
      if (user == member->end() || !user->is_object())
        return {};
      const auto userId = user->find("id");
      if (userId == user->end() || !userId->is_string() || *userId != *id)
        return {};
      MemberRow row;
      row.user = parseAccount(*user);
      if (row.user.id != id->get_ref<const std::string&>() || !seen.insert(row.user.id).second)
        return {};
      if (const auto nick = member->find("nick"); nick != member->end() && nick->is_string())
        row.user.name = nick->get<std::string>();
      const auto presence = entry.find("presence");
      if (presence != entry.end() && presence->is_object())
        if (const auto status = presence->find("status"); status != presence->end() && status->is_string()) {
          const auto value = status->get<std::string>();
          if (value == "online" || value == "idle" || value == "dnd")
            row.status = value;
        }
      row.index = static_cast<int>(rows.size());
      rows.push_back(std::move(row));
    }
    return rows;
  }
} // namespace api
