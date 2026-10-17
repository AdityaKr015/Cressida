#include "discord/role_styles.h"
#include "discord/models.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace discord {
  namespace {
    using Json = nlohmann::json;
    std::string text(const Json& value, const char* key) {
      const auto at = value.find(key);
      return at != value.end() && at->is_string() ? at->get<std::string>() : "";
    }
    std::optional<std::uint32_t> color(const Json& value, const char* key) {
      const auto at = value.find(key);
      if (at == value.end() || !at->is_number_integer())
        return {};
      if (at->is_number_unsigned()) {
        const auto number = at->get<std::uint64_t>();
        return number <= 0xFFFFFF ? std::optional(static_cast<std::uint32_t>(number)) : std::nullopt;
      }
      const auto number = at->get<std::int64_t>();
      return number >= 0 && number <= 0xFFFFFF ? std::optional(static_cast<std::uint32_t>(number)) : std::nullopt;
    }
    std::optional<ColoredRole> role(const Json& value) {
      const auto id = text(value, "id");
      if (!validSnowflake(id))
        return {};
      ColoredRole result;
      result.id = id;
      if (value.contains("position") && value["position"].is_number_integer())
        result.position = static_cast<int>(std::clamp<std::int64_t>(value["position"].get<std::int64_t>(), 0, 1000000));
      result.colors.primary = color(value, "color").value_or(0);
      if (value.contains("colors") && value["colors"].is_object()) {
        const auto& colors = value["colors"];
        result.colors.primary = color(colors, "primary_color").value_or(result.colors.primary);
        result.colors.secondary = color(colors, "secondary_color");
        if (result.colors.secondary)
          result.colors.tertiary = color(colors, "tertiary_color");
      }
      return result;
    }
    bool higher(const ColoredRole& a, const ColoredRole& b) {
      if (a.position != b.position)
        return a.position > b.position;
      // Equal positions: older (smaller snowflake) roles rank higher.
      return a.id.size() != b.id.size() ? a.id.size() < b.id.size() : a.id < b.id;
    }
  }
  std::optional<std::vector<std::string>> parseMemberRoles(const Json& member) {
    const auto at = member.find("roles");
    if (at == member.end() || !at->is_array() || at->size() > 250)
      return {};
    std::vector<std::string> result;
    for (const auto& id : *at) {
      if (!id.is_string() || !validSnowflake(id.get_ref<const std::string&>()))
        return {};
      result.push_back(id.get<std::string>());
    }
    return result;
  }

  RoleUpdates parseRoleUpdates(std::string_view type, const Json& data) {
    RoleUpdates result;
    if (!data.is_object())
      return result;
    result.reset = type == "READY";
    auto member = [](GuildRoleUpdate& update, const Json& value) {
      auto id = text(value, "user_id");
      if (value.contains("user"))
        id = text(value["user"], "id");
      if (validSnowflake(id) && update.members.size() < 10000)
        if (auto roles = parseMemberRoles(value))
          update.members[id] = std::move(*roles);
    };
    auto guild = [&](const Json& value) {
      GuildRoleUpdate update;
      update.guild = text(value, "id");
      if (!validSnowflake(update.guild))
        return update;
      if (value.contains("roles") && value["roles"].is_array()) {
        update.replaceRoles = true;
        for (const auto& item : value["roles"])
          if (update.roles.size() < 1000)
            if (auto parsed = role(item))
              update.roles.push_back(std::move(*parsed));
      }
      const auto& properties = value.contains("properties") && value["properties"].is_object() ? value["properties"] : value;
      if (properties.contains("features") && properties["features"].is_array()) {
        update.enhanced = false;
        for (const auto& feature : properties["features"])
          if (feature == "ENHANCED_ROLE_COLORS")
            update.enhanced = true;
      }
      if (value.contains("members") && value["members"].is_array())
        for (const auto& item : value["members"])
          member(update, item);
      return update;
    };
    if (type == "READY" || type == "READY_SUPPLEMENTAL") {
      if (data.contains("guilds") && data["guilds"].is_array())
        for (std::size_t i = 0; i < data["guilds"].size() && i < 1000; ++i) {
          auto update = guild(data["guilds"][i]);
          if (data.contains("merged_members") && data["merged_members"].is_array()
              && i < data["merged_members"].size() && data["merged_members"][i].is_array())
            for (const auto& item : data["merged_members"][i])
              member(update, item);
          result.guilds.push_back(std::move(update));
        }
    } else if (type == "GUILD_CREATE" || type == "GUILD_UPDATE")
      result.guilds.push_back(guild(data));
    else if (type == "GUILD_DELETE") {
      if (!data.contains("unavailable") || data["unavailable"] != true)
        result.guilds.push_back({.guild = text(data, "id"), .removed = true});
    } else if (type == "GUILD_ROLE_CREATE" || type == "GUILD_ROLE_UPDATE" || type == "GUILD_ROLE_DELETE") {
      GuildRoleUpdate update{.guild = text(data, "guild_id")};
      if (type == "GUILD_ROLE_DELETE")
        update.removedRoles.push_back(text(data, "role_id"));
      else if (data.contains("role"))
        if (auto parsed = role(data["role"]))
          update.roles.push_back(std::move(*parsed));
      result.guilds.push_back(std::move(update));
    } else if (type == "GUILD_MEMBER_UPDATE" || type == "GUILD_MEMBER_ADD" || type == "GUILD_MEMBER_REMOVE") {
      GuildRoleUpdate update{.guild = text(data, "guild_id")};
      if (type == "GUILD_MEMBER_REMOVE")
        update.removedMembers.push_back(data.contains("user") ? text(data["user"], "id") : text(data, "user_id"));
      else
        member(update, data);
      result.guilds.push_back(std::move(update));
    } else if (type == "MESSAGE_CREATE" || type == "MESSAGE_UPDATE") {
      if (data.contains("member") && data["member"].is_object() && data.contains("author")) {
        GuildRoleUpdate update{.guild = text(data, "guild_id")};
        auto value = data["member"];
        value["user_id"] = text(data["author"], "id");
        member(update, value);
        result.guilds.push_back(std::move(update));
      }
    } else if (type == "GUILD_MEMBERS_CHUNK") {
      GuildRoleUpdate update{.guild = text(data, "guild_id")};
      if (data.contains("members") && data["members"].is_array())
        for (const auto& item : data["members"])
          member(update, item);
      result.guilds.push_back(std::move(update));
    } else if (type == "GUILD_MEMBER_LIST_UPDATE") {
      GuildRoleUpdate update{.guild = text(data, "guild_id")};
      if (data.contains("ops") && data["ops"].is_array())
        for (const auto& op : data["ops"]) {
          auto item = [&](const Json& value) {
            if (value.contains("member"))
              member(update, value["member"]);
          };
          if (op.contains("items") && op["items"].is_array())
            for (const auto& entry : op["items"])
              item(entry);
          if (op.contains("item"))
            item(op["item"]);
        }
      result.guilds.push_back(std::move(update));
    }
    return result;
  }

  void RoleDirectory::apply(const RoleUpdates& updates) {
    if (updates.reset) {
      m_guilds.clear();
      m_memberCount = 0;
    }
    for (const auto& update : updates.guilds) {
      if (!validSnowflake(update.guild))
        continue;
      if (update.removed) {
        if (const auto at = m_guilds.find(update.guild); at != m_guilds.end()) {
          m_memberCount -= at->second.members.size();
          m_guilds.erase(at);
        }
        continue;
      }
      if (!m_guilds.contains(update.guild) && m_guilds.size() >= 1000)
        continue;
      auto& target = m_guilds[update.guild];
      if (update.enhanced)
        target.enhanced = *update.enhanced;
      if (update.replaceRoles)
        target.roles.clear();
      for (const auto& role : update.roles)
        if (validSnowflake(role.id) && (target.roles.contains(role.id) || target.roles.size() < 1000))
          target.roles[role.id] = role;
      for (const auto& id : update.removedRoles)
        target.roles.erase(id);
      for (const auto& id : update.removedMembers)
        m_memberCount -= target.members.erase(id);
      for (const auto& [id, roles] : update.members)
        if (validSnowflake(id) && roles.size() <= 250) {
          const bool exists = target.members.contains(id);
          if (!exists && m_memberCount >= 20000)
            continue;
          target.members[id] = roles;
          if (!exists)
            ++m_memberCount;
        }
    }
  }
  std::optional<RoleColors> RoleDirectory::colors(
      const std::string& guild, const std::string& user,
      const std::optional<std::vector<std::string>>& fallbackRoles
  ) const {
    const auto at = m_guilds.find(guild);
    if (at == m_guilds.end() || !validSnowflake(user))
      return {};
    const auto& target = at->second;
    const auto known = target.members.find(user);
    const auto* roles = known != target.members.end() ? &known->second : fallbackRoles ? &*fallbackRoles : nullptr;
    if (!roles)
      return {};
    const ColoredRole* best = nullptr;
    auto consider = [&](const std::string& id) {
      const auto role = target.roles.find(id);
      if (role != target.roles.end() && role->second.colors.primary != 0
          && (!best || higher(role->second, *best)))
        best = &role->second;
    };
    consider(guild);
    for (const auto& id : *roles)
      consider(id);
    if (!best)
      return {};
    auto result = best->colors;
    if (!target.enhanced) {
      result.secondary.reset();
      result.tertiary.reset();
    }
    return result;
  }
} // namespace discord
