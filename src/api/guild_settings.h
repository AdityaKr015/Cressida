#pragma once

#include "api/models.h"

#include <nlohmann/json.hpp>
#include <span>

namespace api {

  enum class GuildSettingsSection {
    Overview,
    Channels,
    Roles,
    Members,
    Bans,
    Invites,
    AuditLog,
    Emoji,
    Stickers,
    Soundboard,
    Integrations,
    Webhooks,
    Widget,
    Welcome,
    Onboarding,
    Vanity,
    Templates,
    Events,
    AutoModeration,
    Prune,
    DeleteGuild,
    MembershipScreening,
    Profile,
    OwnershipCode,
    Applications,
    Discovery,
    Insights,
    ServerGuide,
    IncidentActions,
    BulkBans,
    JoinRequests,
    JoinInterview,
    BoostSlots,
    Boosts,
    CommandPermissions,
    InstalledApplications,
    ApplicationCommands,
    GuideAction,
    GuideResource,
    DiscoveryCategory,
    Access,
    MonetizationRequirements,
    MonetizationRestrictions
  };

  enum class GuildSettingsVerb { Get, Create, Update, Delete, Reorder, Sync };

  struct GuildSettingsRequest {
    GuildSettingsSection section = GuildSettingsSection::Overview;
    GuildSettingsVerb verb = GuildSettingsVerb::Get;
    std::string guildId, resourceId;
    nlohmann::json values = nlohmann::json::object();
    std::string query, cursor, reason;
    unsigned limit = 100;
    std::uint64_t request = 0;
    std::uint64_t mfaChallenge = 0;
    std::string mfaType, mfaCode;
    bool operator==(const GuildSettingsRequest&) const = default;
  };

  struct GuildSettingsHttpRequest {
    std::string path, method = "GET";
    nlohmann::json body;
    // The only multipart resource is a newly uploaded guild sticker.
    bool stickerUpload = false;
  };

  struct GuildSettingsResult {
    nlohmann::json data;
    bool more = false;
    std::string cursor;
  };

  // All routes and writable fields are selected here. UI strings never become routes.
  std::optional<GuildSettingsHttpRequest> guildSettingsHttpRequest(const GuildSettingsRequest& request);
  GuildSettingsResult parseGuildSettings(const nlohmann::json& value, const GuildSettingsRequest& request);
  nlohmann::json guildDiscoveryPatch(const nlohmann::json& current, const nlohmann::json& changes);
  nlohmann::json guildGuidePatch(const nlohmann::json& current, const nlohmann::json& changes);
  struct GuildAccessPlan {
    nlohmann::json guildPatch, verificationPatch;
    std::string mode;
  };
  GuildAccessPlan
  guildAccessPlan(const nlohmann::json& guild, const nlohmann::json& screening, const GuildSettingsRequest& request);
  std::string guildAccessMode(const nlohmann::json& guild);
  struct GuildAuditAction {
    int type;
    std::string_view name, group;
  };
  std::span<const GuildAuditAction> guildAuditActions();
  std::string_view guildAuditActionLabel(int type);
  // Fixed public CDN routes, with all original expression metadata preserved.
  std::string guildExpressionAsset(GuildSettingsSection section, const nlohmann::json& value);

  // Guild-level access and role hierarchy are distinct. Owner/admin bypass applies
  // to permissions, but no member can moderate the owner or edit managed roles.
  struct GuildSettingsAccess {
    std::string self, owner;
    std::string targetCreator;
    std::optional<std::uint64_t> permissions;
    int selfPosition = -1, targetPosition = -1;
    bool targetManaged = false;
  };
  std::uint64_t guildSettingsPermission(const GuildSettingsRequest& request);
  bool canManageGuildSettings(const GuildSettingsRequest& request, const GuildSettingsAccess& access);
  // Toggle one known permission without rounding or dropping unknown higher bits.
  std::string guildRolePermission(std::string decimal, unsigned bit, bool enabled);

} // namespace api
