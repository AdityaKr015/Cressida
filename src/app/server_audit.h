#pragma once

#include "api/guild_settings.h"

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace app {
  struct AuditDetail {
    std::string title, before, after;
    bool hadBefore = false, hasAfter = false;
  };

  // Audit responses retain their supplemental objects. Deleted or archived
  // targets can therefore be named without issuing unrelated account requests.
  struct AuditReferences {
    std::map<std::string, nlohmann::json> users, resources;
    void merge(const nlohmann::json& response);
    std::string user(const std::string& id) const;
    std::string resource(const std::string& id) const;
  };

  using AuditLookup = std::function<std::string(const std::string&)>;
  std::string auditEntryId(const nlohmann::json& entry);
  std::string auditEntryTime(const nlohmann::json& entry, bool dateOnly = false);
  std::string auditEntryTarget(const nlohmann::json& entry, const AuditLookup& lookup);
  std::string auditEntrySummary(const nlohmann::json& entry, const AuditLookup& lookup);
  std::vector<AuditDetail> auditEntryDetails(const nlohmann::json& entry, const AuditLookup& lookup);
  std::string auditValue(const std::string& key, const nlohmann::json& value, const AuditLookup& lookup);

  struct ServerAuditState {
    std::string guild, actor, actorQuery, cursor, error;
    int action = 0;
    bool loaded = false, loading = false, more = false;
    std::optional<api::GuildSettingsRequest> pending, failed;
    nlohmann::json entries = nlohmann::json::array();
    AuditReferences references;
    std::set<std::string> expanded;

    void replace(const nlohmann::json& response, bool append);
  };
} // namespace app
