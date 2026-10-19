#pragma once

#include <cstdint>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace api {
  inline constexpr std::uint64_t guildOptInChannelsOn = 1ULL << 14;
  inline constexpr std::uint64_t guildOptInChannelsOff = 1ULL << 13;
  inline constexpr std::uint64_t channelOptInEnabled = 1ULL << 12;
  // A partial server preference update. Missing members preserve the account's
  // existing values; these are personal preferences, not server configuration.
  struct GuildNotification {
    std::optional<bool> muted, suppressEveryone, suppressRoles, mobilePush, hideMutedChannels, muteScheduledEvents;
    std::optional<std::string> muteUntil;
    std::optional<int> level, notifyHighlights;
    std::optional<std::uint64_t> flags;
    bool empty() const;
    bool operator==(const GuildNotification&) const = default;
  };
  std::map<std::string, GuildNotification> guildNotifications(const nlohmann::json& settings);
  std::optional<nlohmann::json> guildNotificationPatch(const GuildNotification& patch, int muteDuration = -1);
  // One queue entry covers the complete read action rather than overflowing the
  // ordinary bounded mutation queue on servers with many unread channels.
  std::optional<nlohmann::json> guildReadBody(const std::vector<std::pair<std::string, std::string>>& states);
} // namespace api
