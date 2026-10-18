#pragma once

#include "api/guild_settings.h"

#include <map>
#include <optional>
#include <set>
#include <string>

namespace app {
  // The editor stores only explicit changes; a save reads the current command
  // record and merges these changes so unrelated administrator edits survive.
  nlohmann::json mergeCommandPermissionEdits(
      const nlohmann::json& current, const nlohmann::json& baseline, const nlohmann::json& draft,
      bool restoreApplicationDefaults = false
  );
  std::string allCommandChannelsId(std::string guild);
  std::string commandPermissionKey(const nlohmann::json& permission);
  nlohmann::json
  editCommandPermission(nlohmann::json permissions, const std::string& id, int type, std::optional<bool> allow);
  struct ServerCommandsState {
    std::string guild, application, command, applicationSearch, commandSearch, targetSearch, target;
    std::string pendingApplication, pendingCommand;
    int targetType = 1;
    bool applicationsLoaded = false, indexLoaded = false, confirming = false, restoringDefaults = false;
    bool applicationsFailed = false, indexFailed = false;
    nlohmann::json applications = nlohmann::json::array(), commands = nlohmann::json::array();
    nlohmann::json baseline = nlohmann::json::array();
    std::map<std::string, nlohmann::json> permissions;
    std::set<std::string> loadedApplications;
    std::set<std::string> failedApplications;
    std::optional<api::GuildSettingsRequest> preparing;
  };
} // namespace app
