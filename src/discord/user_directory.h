#pragma once

#include "discord/models.h"

namespace discord {
  struct UserUpdate {
    std::optional<std::string> nickname, status;
  };
  struct UserUpdates {
    bool reset = false;
    std::map<std::string, UserUpdate> users;
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

  private:
    std::map<std::string, std::string> m_names, m_statuses, m_globalStatuses;
    std::map<std::string, std::map<std::string, std::string>> m_guildStatuses;
  };
} // namespace discord
