#pragma once

#include <cstdint>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace api {
  struct Channel;
  struct RoleColors {
    std::uint32_t primary = 0;
    std::optional<std::uint32_t> secondary, tertiary;
    bool operator==(const RoleColors&) const = default;
  };
  struct ColoredRole {
    std::string id;
    int position = 0;
    RoleColors colors;
    std::string name;
    std::string icon, unicodeEmoji;
    std::optional<std::string> permissions;
  };
  struct GuildRoleUpdate {
    std::string guild;
    bool removed = false, replaceRoles = false;
    std::optional<bool> enhanced;
    std::vector<ColoredRole> roles;
    std::vector<std::string> removedRoles, removedMembers;
    std::map<std::string, std::vector<std::string>> members;
    std::optional<std::string> owner;
  };
  struct RoleUpdates {
    bool reset = false;
    std::vector<GuildRoleUpdate> guilds;
  };
  RoleUpdates parseRoleUpdates(std::string_view type, const nlohmann::json& data);
  std::optional<std::vector<std::string>> parseMemberRoles(const nlohmann::json& member);

  // Application-owned, guild-scoped role/membership cache. Raw Gateway payloads
  // are projected on the worker; no rendering types cross the backend boundary.
  class RoleDirectory {
  public:
    void apply(const RoleUpdates& updates);
    std::vector<ColoredRole> allRoles(const std::string& guild) const;
    std::string roleName(const std::string& guild, const std::string& role) const;
    std::optional<std::vector<std::string>> memberships(const std::string& guild, const std::string& user) const;
    std::string owner(const std::string& guild) const;
    std::optional<RoleColors> colors(
        const std::string& guild, const std::string& user,
        const std::optional<std::vector<std::string>>& fallbackRoles = {}
    ) const;
    std::vector<ColoredRole> roles(
        const std::string& guild, const std::string& user,
        const std::optional<std::vector<std::string>>& fallbackRoles = {}
    ) const;
    std::optional<ColoredRole> icon(
        const std::string& guild, const std::string& user,
        const std::optional<std::vector<std::string>>& fallbackRoles = {}
    ) const;

  private:
    struct GuildRoles {
      bool enhanced = true;
      std::string owner;
      std::map<std::string, ColoredRole> roles;
      std::map<std::string, std::vector<std::string>> members;
    };
    std::map<std::string, GuildRoles> m_guilds;
    std::size_t m_memberCount = 0;
  };
} // namespace api
