#include "app/server_audit.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

int main() {
  using Json = nlohmann::json;
  const auto require = [](bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  };
  try {
    app::ServerAuditState state;
    state.replace(
        {{"users", Json::array({{{"id", "10"}, {"username", "before"}, {"global_name", "Mira"}}})},
         {"threads", Json::array({{{"id", "20"}, {"name", "Archived discussion"}}})},
         {"webhooks", Json::array({{{"id", "30"}, {"name", "Release updates"}}})},
         {"audit_log_entries",
          Json::array({{{"id", "100"}, {"action_type", 110}, {"user_id", "10"}, {"target_id", "20"}}})}},
        false
    );
    state.replace(
        {{"users", Json::array({{{"id", "11"}, {"username", "lysec"}}})},
         {"audit_log_entries",
          Json::array(
              {{{"id", "99"}, {"action_type", 51}, {"user_id", "11"}, {"target_id", "30"}},
               {{"id", "100"}, {"action_type", 111}, {"user_id", "10"}, {"target_id", "20"}}}
          )}},
        true
    );
    require(
        state.entries.size() == 2 && state.entries[0]["id"] == "100" && state.entries[1]["id"] == "99",
        "Paged audit logs merge duplicate IDs and sort snowflakes numerically"
    );
    require(
        state.references.user("10") == "Mira"
            && state.references.user("11") == "lysec"
            && state.references.resource("20") == "Archived discussion"
            && state.references.resource("30") == "Release updates",
        "Supplemental actor and deleted/archived resource records survive pagination"
    );
    const app::AuditLookup lookup = [&](const std::string& id) {
      const auto user = state.references.user(id);
      return user.empty() ? state.references.resource(id) : user;
    };
    require(
        app::auditEntrySummary(state.entries[0], lookup) == "Mira updated thread Archived discussion",
        "Audit summary resolves actor and archived thread names"
    );
    require(
        app::auditEntrySummary(state.entries[1], lookup) == "lysec updated webhook Release updates",
        "Audit summary resolves webhook targets using response metadata"
    );
    const Json deleted = {
        {"id", "98"},
        {"action_type", 32},
        {"user_id", "10"},
        {"target_id", "40"},
        {"changes", Json::array({{{"key", "name"}, {"old_value", "Deleted role"}}})}
    };
    require(
        app::auditEntrySummary(deleted, lookup) == "Mira deleted role Deleted role",
        "Deleted role names fall back to the audit change snapshot"
    );
    const auto reset =
        app::auditEntryDetails({{"changes", Json::array({{{"key", "topic"}, {"old_value", "Old topic"}}})}}, lookup);
    require(
        reset.size() == 1
            && reset[0].hadBefore
            && !reset[0].hasAfter
            && reset[0].before == "Old topic"
            && reset[0].after == "Not set",
        "Missing new_value is an explicit property reset, not an unchanged value"
    );
    const auto high = app::auditEntryDetails(
        {{"changes",
          Json::array(
              {{{"key", "permissions"}, {"old_value", "1024"}, {"new_value", "1267650600228229401496703206400"}}}
          )}},
        lookup
    );
    require(
        high.size() == 1
            && high[0].title == "Permissions · Permission bit 100"
            && high[0].before == "Disabled"
            && high[0].after == "Enabled",
        "Permission change summaries retain unknown bits beyond uint64 without rounding"
    );
    const auto removed = app::auditEntryDetails(
        {{"changes",
          Json::array({{{"key", "$remove"}, {"new_value", Json::array({{{"id", "50"}, {"name", "Team"}}})}}})}},
        lookup
    );
    require(
        removed.size() == 1 && removed[0].before == "Team" && removed[0].after == "Removed",
        "Role removal uses Discord's exceptional new_value role list"
    );
    const auto options = app::auditEntryDetails(
        {{"options", {{"channel_id", "20"}, {"count", "12"}, {"future_key", "Preserved"}}}}, lookup
    );
    require(
        options.size() == 3
            && std::ranges::any_of(options, [](const auto& option) { return option.after == "Archived discussion"; })
            && std::ranges::any_of(
                options, [](const auto& option) { return option.title == "Future key" && option.after == "Preserved"; }
            ),
        "Audit option details resolve known IDs and retain future fields"
    );
    const auto overwrite = app::auditValue(
        "permission_overwrites", Json::array({{{"id", "10"}, {"type", 1}, {"allow", "1024"}, {"deny", "2048"}}}), lookup
    );
    require(
        overwrite.find("Mira") != std::string::npos
            && overwrite.find("View channels") != std::string::npos
            && overwrite.find("Send messages") != std::string::npos,
        "Channel overwrite details render named subjects with readable allowed and denied permissions"
    );
    require(
        app::auditEntrySummary({{"action_type", 999}, {"user_id", nullptr}, {"target_id", "unknown"}}, lookup)
            == "Discord performed action 999 unknown",
        "Future audit actions and null actors remain readable"
    );
    require(
        app::auditEntryTime({{"id", "invalid"}}) == "Unknown date",
        "Invalid snowflakes do not invent an audit timestamp"
    );
    state.replace({{"audit_log_entries", Json::array({{{"id", "200"}}})}}, false);
    require(
        state.entries.size() == 1 && state.entries[0]["id"] == "200",
        "A filtered replacement cannot retain unrelated earlier rows"
    );
    std::cout << "server audit tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
