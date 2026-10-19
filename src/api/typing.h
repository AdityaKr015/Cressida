#pragma once

#include "api/models.h"

#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace api {
  struct TypingStart {
    std::string channel, guild, user, name;
    std::int64_t timestamp = 0;
  };
  inline constexpr std::size_t maximumTypingNameBytes = 128;

  inline std::optional<TypingStart> parseTypingStart(const nlohmann::json& value) {
    if (!value.is_object())
      return {};
    const auto id = [&](const char* key) {
      const auto at = value.find(key);
      return at != value.end() && at->is_string() ? at->get<std::string>() : std::string{};
    };
    if (const auto guild = value.find("guild_id"); guild != value.end() && !guild->is_null() && !guild->is_string())
      return {};
    TypingStart result{.channel = id("channel_id"), .guild = id("guild_id"), .user = id("user_id")};
    if (!validSnowflake(result.channel)
        || !validSnowflake(result.user)
        || (!result.guild.empty() && !validSnowflake(result.guild)))
      return {};
    const auto timestamp = value.find("timestamp");
    if (timestamp == value.end()
        || !timestamp->is_number_integer()
        || (timestamp->is_number_unsigned()
            && timestamp->get<std::uint64_t>() > std::numeric_limits<std::int64_t>::max()))
      return {};
    result.timestamp = timestamp->get<std::int64_t>();
    if (result.timestamp < 0)
      return {};
    if (const auto member = value.find("member"); member != value.end() && member->is_object()) {
      if (const auto user = member->find("user"); user != member->end() && user->is_object()) {
        const auto account = parseAccount(*user);
        if (account.id != result.user)
          return {};
        result.name = account.name;
      }
      if (const auto nick = member->find("nick"); nick != member->end() && nick->is_string() && !nick->empty())
        result.name = nick->get<std::string>();
    }
    if (result.name.size() > maximumTypingNameBytes)
      result.name.clear();
    return result;
  }
} // namespace api
