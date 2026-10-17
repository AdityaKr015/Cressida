#pragma once
#include "discord/models.h"

#include <array>

namespace discord {
  struct ProfileBadge {
    std::string description, image;
  };
  struct UserProfile {
    Account user;
    std::string guild, banner, decoration, guildTag, guildBadge, joinedAt;
    std::optional<std::uint32_t> accent;
    std::optional<std::array<std::uint32_t, 2>> theme;
    std::vector<ProfileBadge> badges;
    std::optional<std::vector<std::string>> roles;
    std::vector<Account> mutualFriends;
    std::optional<int> mutualFriendCount, mutualGuildCount;
    bool privateProfile = false;
  };
  // Whitelist presentation data only. Authentication/analytics fields never
  // leave the REST worker, and all image paths are fixed Discord CDN routes.
  UserProfile parseUserProfile(const nlohmann::json& value, const std::string& guild = {});

  struct GuildTrait {
    std::string name, emoji;
  };
  struct GuildProfile {
    std::string id, name, icon, banner, description;
    std::optional<GuildTag> tag;
    std::optional<int> members, online;
    std::optional<std::uint32_t> accent;
    std::vector<GuildTrait> traits;
    bool joinable = false, recruitment = false, verified = false, partnered = false;
  };
  GuildProfile parseGuildProfile(const nlohmann::json& value);
} // namespace discord
