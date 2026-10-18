#pragma once

#include "api/models.h"

#include <chrono>

namespace api {
  struct ChannelNotification {
    std::optional<int> level;
    std::optional<bool> muted;
    std::optional<std::string> muteUntil;
  };
  std::map<std::string, ChannelNotification> channelNotifications(const nlohmann::json& settings);
  std::optional<std::chrono::system_clock::time_point> muteDeadline(std::string_view text);
  struct UserUpdate {
    std::optional<std::string> nickname, status;
    std::optional<int> relationship;
    std::optional<std::optional<GuildTag>> guildTag;
    std::optional<bool> ignored;
  };
  struct UserUpdates {
    bool reset = false;
    std::map<std::string, UserUpdate> users;
    std::map<std::string, bool> mutedChannels;
    std::map<std::string, ChannelNotification> notifications;
    bool relationshipsReset = false;
  };

  // Gateway-owned protocol tracking, or an event-loop-owned view of typed updates.
  // Keep personal nicknames separate from global and guild display names so that
  // removing an alias immediately restores the original name everywhere.
  class UserDirectory {
  public:
    UserUpdates consume(std::string_view type, const nlohmann::json& data);
    void apply(const UserUpdates& update);
    std::string name(const std::string& user, const std::string& fallback) const;
    std::string status(const std::string& user, const std::string& fallback = "offline") const;
    int relationship(const std::string& user) const;
    bool ignored(const std::string& user) const;
    std::uint64_t relationshipRevision() const { return m_relationshipRevision; }
    bool muted(const std::string& channel) const;
    int notificationLevel(const std::string& channel) const;
    std::uint64_t notificationRevision(const std::string& channel) const;
    std::optional<std::chrono::system_clock::time_point> nextMuteExpiry() const;
    std::optional<GuildTag> guildTag(const std::string& user, const std::optional<GuildTag>& fallback = {}) const;

  private:
    std::map<std::string, std::string> m_names, m_statuses, m_globalStatuses;
    std::map<std::string, std::map<std::string, std::string>> m_guildStatuses;
    std::map<std::string, int> m_relationships;
    std::map<std::string, bool> m_ignored;
    std::uint64_t m_relationshipRevision = 0;
    std::map<std::string, std::optional<GuildTag>> m_tags;
    std::map<std::string, bool> m_mutedChannels;
    std::map<std::string, ChannelNotification> m_notifications;
    std::map<std::string, std::uint64_t> m_notificationRevisions;
    std::uint64_t m_notificationSerial = 0;
  };
} // namespace api
