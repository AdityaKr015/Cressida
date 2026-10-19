#pragma once

#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>

namespace api {
  struct GuildPrivacy {
    std::optional<bool> directMessages = {}, activity = {}, activityJoining = {}, messageRequests = {};
    bool empty() const { return !directMessages && !activity && !activityJoining && !messageRequests; }
  };

  // Resolved values use true for allowed and false for restricted. Saves change
  // only engaged options and preserve every other guild and privacy setting.
  GuildPrivacy parseGuildPrivacy(const nlohmann::json& snapshot, const std::string& guild);
  nlohmann::json
  guildPrivacyPatch(const nlohmann::json& snapshot, const std::string& guild, const GuildPrivacy& changes);
} // namespace api
