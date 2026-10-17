#pragma once

#include "discord/models.h"

namespace discord {
  struct UserUpdate {
    std::optional<std::string> nickname, status;
    std::optional<int> relationship;
  };
  struct UserUpdates {
    bool reset = false;
    std::map<std::string, UserUpdate> users;
    std::map<std::string, bool> mutedChannels;
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
    bool muted(const std::string& channel) const;

  private:
    std::map<std::string, std::string> m_names, m_statuses, m_globalStatuses;
    std::map<std::string, std::map<std::string, std::string>> m_guildStatuses;
    std::map<std::string, int> m_relationships;
    std::map<std::string, bool> m_mutedChannels;
  };
} // namespace discord
