#pragma once

#include <cstdint>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace discord {
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
  };
  struct GuildRoleUpdate {
    std::string guild;
    bool removed = false, replaceRoles = false;
    std::optional<bool> enhanced;
    std::vector<ColoredRole> roles;
    std::vector<std::string> removedRoles, removedMembers;
    std::map<std::string, std::vector<std::string>> members;
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
    std::optional<RoleColors> colors(
        const std::string& guild, const std::string& user,
        const std::optional<std::vector<std::string>>& fallbackRoles = {}
    ) const;
    std::vector<ColoredRole> roles(const std::string& guild, const std::string& user,
                                 const std::optional<std::vector<std::string>>& fallbackRoles = {}) const;
    std::optional<ColoredRole> icon(const std::string& guild, const std::string& user,
                                 const std::optional<std::vector<std::string>>& fallbackRoles = {}) const;
  private:
    struct GuildRoles {
      bool enhanced = true;
      std::map<std::string, ColoredRole> roles;
      std::map<std::string, std::vector<std::string>> members;
    };
    std::map<std::string, GuildRoles> m_guilds;
    std::size_t m_memberCount = 0;
  };
} // namespace discord
