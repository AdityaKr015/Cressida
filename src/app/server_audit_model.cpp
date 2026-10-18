#include "app/server_audit.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace app {
  using Json = nlohmann::json;
  namespace {
    std::string text(const Json& object, std::string_view key, std::string fallback = {}) {
      if (!object.is_object())
        return fallback;
      const auto at = object.find(key);
      if (at == object.end() || at->is_null())
        return fallback;
      return at->is_string() ? at->get<std::string>() : at->is_number() ? at->dump() : fallback;
    }
    std::string title(std::string key) {
      if (key.empty())
        return "Value";
      std::ranges::replace(key, '_', ' ');
      key.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(key.front())));
      return key;
    }
    std::string join(const std::vector<std::string>& values, std::string_view separator = ", ") {
      std::string result;
      for (const auto& value : values) {
        if (!result.empty())
          result += separator;
        result += value;
      }
      return result;
    }
    constexpr std::array<std::string_view, 53> permissionNames = {
        "Create invites",
        "Kick members",
        "Ban members",
        "Administrator",
        "Manage channels",
        "Manage server",
        "Add reactions",
        "View audit log",
        "Priority speaker",
        "Stream video",
        "View channels",
        "Send messages",
        "Send text-to-speech messages",
        "Manage messages",
        "Embed links",
        "Attach files",
        "Read message history",
        "Mention everyone and all roles",
        "Use external emoji",
        "View server insights",
        "Connect",
        "Speak",
        "Mute members",
        "Deafen members",
        "Move members",
        "Use voice activity",
        "Change nickname",
        "Manage nicknames",
        "Manage roles",
        "Manage webhooks",
        "Manage expressions",
        "Use application commands",
        "Request to speak",
        "Manage events",
        "Manage threads",
        "Create public threads",
        "Create private threads",
        "Use external stickers",
        "Send messages in threads",
        "Start embedded activities",
        "Timeout members",
        "View creator monetization",
        "Use soundboard",
        "Create expressions",
        "Create events",
        "Use external sounds",
        "Send voice messages",
        "Permission bit 47",
        "Set voice channel status",
        "Send polls",
        "Use external apps",
        "Pin messages",
        "Bypass slowmode"
    };
    std::vector<unsigned> bits(const Json& value) {
      std::string decimal = value.is_string()                           ? value.get<std::string>()
          : value.is_number_unsigned()                                  ? value.dump()
          : value.is_number_integer() && value.get<std::int64_t>() >= 0 ? value.dump()
                                                                        : "";
      if (decimal.empty() || decimal.size() > 256 || !std::ranges::all_of(decimal, [](unsigned char c) {
            return c >= '0' && c <= '9';
          }))
        return {};
      std::vector<unsigned> result;
      for (unsigned bit = 0; decimal != "0"; ++bit) {
        unsigned carry = 0;
        std::string quotient;
        for (const char c : decimal) {
          const auto n = carry * 10 + static_cast<unsigned>(c - '0');
          if (!quotient.empty() || n / 2)
            quotient += static_cast<char>('0' + n / 2);
          carry = n % 2;
        }
        if (carry)
          result.push_back(bit);
        decimal = quotient.empty() ? "0" : quotient;
      }
      return result;
    }
    std::string permissionName(unsigned bit) {
      return bit < permissionNames.size() ? std::string(permissionNames[bit]) : "Permission bit " + std::to_string(bit);
    }
    std::string permissions(const Json& value) {
      std::vector<std::string> result;
      for (const auto bit : bits(value))
        result.push_back(permissionName(bit));
      return result.empty() ? "None" : join(result);
    }
    std::string named(const std::string& id, const AuditLookup& lookup) {
      if (id.empty())
        return "Not set";
      const auto resolved = lookup ? lookup(id) : std::string{};
      return resolved.empty() ? id : resolved;
    }
    std::string changedName(const Json& entry) {
      if (entry.contains("changes") && entry["changes"].is_array())
        for (const auto& change : entry["changes"])
          if (text(change, "key") == "name" || text(change, "key") == "code") {
            const auto current = text(change, "new_value", text(change, "old_value"));
            if (!current.empty())
              return current;
          }
      return {};
    }
    std::string fieldName(const std::string& key) {
      static const std::map<std::string, std::string> names = {
          {"$add", "Roles added"},
          {"$remove", "Roles removed"},
          {"afk_channel_id", "Inactive channel"},
          {"afk_timeout", "Inactive timeout"},
          {"allow", "Allowed permissions"},
          {"deny", "Denied permissions"},
          {"permissions", "Permissions"},
          {"permission_overwrites", "Channel permission overrides"},
          {"default_message_notifications", "Default notifications"},
          {"explicit_content_filter", "Media scanning"},
          {"verification_level", "Verification level"},
          {"mfa_level", "Moderator two-factor requirement"},
          {"communication_disabled_until", "Timeout until"},
          {"rate_limit_per_user", "Slowmode"},
          {"default_thread_rate_limit_per_user", "Default thread slowmode"},
          {"parent_id", "Category / parent channel"},
          {"channel_id", "Channel"},
          {"owner_id", "Server owner"},
          {"system_channel_id", "System messages channel"},
          {"rules_channel_id", "Rules channel"},
          {"public_updates_channel_id", "Community updates channel"},
          {"preferred_locale", "Server language"},
          {"max_age", "Invite expiry"},
          {"max_uses", "Invite use limit"},
          {"nsfw", "Age-restricted channel"},
          {"hoist", "Display role separately"},
          {"mentionable", "Mentionable role"},
          {"nick", "Nickname"},
          {"deaf", "Server deafened"},
          {"mute", "Server muted"},
          {"auto_archive_duration", "Hide after inactivity"},
          {"default_auto_archive_duration", "Default thread inactivity"},
          {"trigger_metadata", "AutoMod trigger"},
          {"exempt_channels", "Exempt channels"},
          {"exempt_roles", "Exempt roles"},
          {"system_channel_flags", "System message options"},
          {"icon_hash", "Server icon"},
          {"avatar_hash", "Avatar"},
          {"banner_hash", "Server banner"},
          {"splash_hash", "Invite background"}
      };
      const auto at = names.find(key);
      return at == names.end() ? title(key) : at->second;
    }
  } // namespace

  void AuditReferences::merge(const Json& response) {
    if (!response.is_object())
      return;
    for (const auto* key :
         {"users", "webhooks", "integrations", "threads", "application_commands", "auto_moderation_rules",
          "guild_scheduled_events"}) {
      const auto list = response.find(key);
      if (list == response.end() || !list->is_array())
        continue;
      for (const auto& record : *list) {
        const auto id = text(record, "id");
        if (!id.empty())
          (std::string_view(key) == "users" ? users : resources)[id] = record;
      }
    }
  }
  std::string AuditReferences::user(const std::string& id) const {
    const auto at = users.find(id);
    if (at == users.end())
      return {};
    const auto name = text(at->second, "global_name");
    return name.empty() ? text(at->second, "username", id) : name;
  }
  std::string AuditReferences::resource(const std::string& id) const {
    const auto at = resources.find(id);
    return at == resources.end() ? "" : text(at->second, "name", id);
  }
  std::string auditEntryId(const Json& entry) { return text(entry, "id"); }
  std::string auditEntryTime(const Json& entry, bool dateOnly) {
    const auto id = auditEntryId(entry);
    std::uint64_t snowflake = 0;
    const auto parsed = std::from_chars(id.data(), id.data() + id.size(), snowflake);
    if (parsed.ec != std::errc{} || parsed.ptr != id.data() + id.size())
      return "Unknown date";
    const auto milliseconds = (snowflake >> 22) + 1420070400000ULL;
    const auto epoch = static_cast<std::time_t>(milliseconds / 1000);
    std::tm local{};
    if (!localtime_r(&epoch, &local))
      return "Unknown date";
    std::array<char, 96> buffer{};
    std::strftime(buffer.data(), buffer.size(), dateOnly ? "%d %B %Y" : "%d %b %Y at %H:%M", &local);
    return buffer.data();
  }
  std::string auditEntryTarget(const Json& entry, const AuditLookup& lookup) {
    const auto id = text(entry, "target_id");
    const auto resolved = lookup && !id.empty() ? lookup(id) : std::string{};
    if (!resolved.empty() && resolved != id)
      return resolved;
    const auto name = changedName(entry);
    if (!name.empty())
      return name;
    if (!id.empty())
      return id;
    return {};
  }
  std::string auditEntrySummary(const Json& entry, const AuditLookup& lookup) {
    const auto type = entry.value("action_type", 0);
    std::string verb;
    switch (type) {
    case 1:
      verb = "updated the server";
      break;
    case 10:
      verb = "created channel";
      break;
    case 11:
      verb = "updated channel";
      break;
    case 12:
      verb = "deleted channel";
      break;
    case 13:
      verb = "added channel permissions for";
      break;
    case 14:
      verb = "updated channel permissions for";
      break;
    case 15:
      verb = "removed channel permissions for";
      break;
    case 16:
      verb = "reordered channels";
      break;
    case 20:
      verb = "kicked";
      break;
    case 21:
      verb = "pruned inactive members";
      break;
    case 22:
      verb = "banned";
      break;
    case 23:
      verb = "unbanned";
      break;
    case 24:
      verb = "updated member";
      break;
    case 25:
      verb = "updated roles for";
      break;
    case 26:
      verb = "moved members to a voice channel";
      break;
    case 27:
      verb = "disconnected members from voice";
      break;
    case 28:
      verb = "added bot";
      break;
    case 30:
      verb = "created role";
      break;
    case 31:
      verb = "updated role";
      break;
    case 32:
      verb = "deleted role";
      break;
    case 33:
      verb = "reordered roles";
      break;
    case 40:
      verb = "created invite";
      break;
    case 41:
      verb = "updated invite";
      break;
    case 42:
      verb = "revoked invite";
      break;
    case 50:
      verb = "created webhook";
      break;
    case 51:
      verb = "updated webhook";
      break;
    case 52:
      verb = "deleted webhook";
      break;
    case 60:
      verb = "added emoji";
      break;
    case 61:
      verb = "updated emoji";
      break;
    case 62:
      verb = "deleted emoji";
      break;
    case 72:
      verb = "deleted messages from";
      break;
    case 73:
      verb = "bulk deleted messages";
      break;
    case 74:
      verb = "pinned a message";
      break;
    case 75:
      verb = "unpinned a message";
      break;
    case 80:
      verb = "added integration";
      break;
    case 81:
      verb = "updated integration";
      break;
    case 82:
      verb = "removed integration";
      break;
    case 83:
      verb = "started a stage";
      break;
    case 84:
      verb = "updated a stage";
      break;
    case 85:
      verb = "ended a stage";
      break;
    case 90:
      verb = "added sticker";
      break;
    case 91:
      verb = "updated sticker";
      break;
    case 92:
      verb = "deleted sticker";
      break;
    case 100:
      verb = "created event";
      break;
    case 101:
      verb = "updated event";
      break;
    case 102:
      verb = "cancelled event";
      break;
    case 110:
      verb = "created thread";
      break;
    case 111:
      verb = "updated thread";
      break;
    case 112:
      verb = "deleted thread";
      break;
    case 121:
      verb = "updated application command permissions";
      break;
    case 130:
      verb = "added soundboard sound";
      break;
    case 131:
      verb = "updated soundboard sound";
      break;
    case 132:
      verb = "deleted soundboard sound";
      break;
    case 140:
      verb = "created AutoMod rule";
      break;
    case 141:
      verb = "updated AutoMod rule";
      break;
    case 142:
      verb = "deleted AutoMod rule";
      break;
    case 143:
      verb = "had a message blocked by AutoMod";
      break;
    case 144:
      verb = "had a message flagged by AutoMod";
      break;
    case 145:
      verb = "was timed out by AutoMod";
      break;
    case 146:
      verb = "was quarantined by AutoMod";
      break;
    case 150:
      verb = "requested creator monetization";
      break;
    case 151:
      verb = "accepted creator monetization terms";
      break;
    case 163:
      verb = "created onboarding question";
      break;
    case 164:
      verb = "updated onboarding question";
      break;
    case 165:
      verb = "deleted onboarding question";
      break;
    case 166:
      verb = "created onboarding";
      break;
    case 167:
      verb = "updated onboarding";
      break;
    case 171:
      verb = "featured a server home item";
      break;
    case 172:
      verb = "removed a server home item";
      break;
    case 180:
      verb = "had a harmful link blocked";
      break;
    case 190:
      verb = "created the server guide";
      break;
    case 191:
      verb = "updated the server guide";
      break;
    case 192:
      verb = "set a voice channel status";
      break;
    case 193:
      verb = "removed a voice channel status";
      break;
    case 200:
      verb = "created a recurring event exception";
      break;
    case 201:
      verb = "updated a recurring event exception";
      break;
    case 202:
      verb = "deleted a recurring event exception";
      break;
    case 210:
      verb = "updated member applications";
      break;
    case 211:
      verb = "updated the server profile";
      break;
    case 212:
      verb = "migrated pin permissions";
      break;
    case 213:
      verb = "migrated slowmode permissions";
      break;
    default:
      verb = "performed action " + std::to_string(type);
      break;
    }
    auto target = auditEntryTarget(entry, lookup);
    if (type >= 13 && type <= 15 && entry.contains("options") && entry["options"].is_object()) {
      const auto& options = entry["options"];
      const auto subject = text(options, "role_name", named(text(options, "id"), lookup));
      if (!subject.empty() && subject != "Not set")
        target = subject + (target.empty() ? "" : " in " + target);
    }
    const auto actorId = text(entry, "user_id");
    const auto actor = actorId.empty() ? "Discord" : named(actorId, lookup);
    if (type == 1
        || type == 16
        || type == 21
        || type == 26
        || type == 27
        || type == 33
        || type == 73
        || type == 121
        || (type >= 143 && type <= 151)
        || type == 166
        || type == 167
        || (type >= 190 && type <= 193)
        || (type >= 210 && type <= 213))
      target.clear();
    return actor + " " + verb + (target.empty() ? "" : " " + target);
  }

  std::string auditValue(const std::string& key, const Json& value, const AuditLookup& lookup) {
    if (value.is_null())
      return "Not set";
    if (key == "permissions" || key == "allow" || key == "deny")
      return permissions(value);
    if (key == "color" && value.is_number_unsigned()) {
      if (value == 0)
        return "Default";
      std::ostringstream out;
      out << '#' << std::uppercase << std::hex << std::setfill('0') << std::setw(6) << value.get<std::uint32_t>();
      return out.str();
    }
    if (value.is_boolean())
      return value.get<bool>() ? "Enabled" : "Disabled";
    if (value.is_number_integer()) {
      const auto number = value.get<int>();
      const auto enumValue = [&](const std::vector<std::string>& names) {
        return number >= 0 && static_cast<std::size_t>(number) < names.size() ? names[number] : std::to_string(number);
      };
      if (key == "verification_level")
        return enumValue(
            {"None", "Low: verified email", "Medium: registered for 5 minutes", "High: member for 10 minutes",
             "Highest: verified phone"}
        );
      if (key == "default_message_notifications")
        return enumValue({"All messages", "Only mentions"});
      if (key == "explicit_content_filter")
        return enumValue({"Do not scan", "Members without roles", "All members"});
      if (key == "mfa_level")
        return enumValue({"Not required", "Required"});
      if (key == "rate_limit_per_user" || key == "default_thread_rate_limit_per_user" || key == "afk_timeout")
        return number ? std::to_string(number) + " seconds" : "Off";
      if (key == "max_age")
        return number ? std::to_string(number) + " seconds" : "Never";
      if (key == "max_uses")
        return number ? std::to_string(number) : "Unlimited";
      if (key == "auto_archive_duration" || key == "default_auto_archive_duration")
        return std::to_string(number) + " minutes";
    }
    if (value.is_string()) {
      const auto string = value.get<std::string>();
      if (key.ends_with("_id") || key == "id")
        return named(string, lookup);
      return string.empty() ? "Empty" : string;
    }
    if (value.is_array()) {
      std::vector<std::string> values;
      for (const auto& item : value) {
        if (item.is_object()) {
          auto name = text(item, "name");
          if (name.empty())
            name = named(text(item, "id"), lookup);
          if (key == "permission_overwrites") {
            name += item.contains("allow") ? "\nAllowed: " + permissions(item["allow"]) : "";
            name += item.contains("deny") ? "\nDenied: " + permissions(item["deny"]) : "";
          } else if (!item.contains("name") && !item.contains("id"))
            name = item.dump();
          values.push_back(std::move(name));
        } else if (item.is_string() && (key == "exempt_roles" || key == "exempt_channels" || key == "roles"))
          values.push_back(named(item.get<std::string>(), lookup));
        else
          values.push_back(auditValue("", item, lookup));
      }
      return values.empty() ? "None" : join(values, key == "permission_overwrites" ? "\n\n" : ", ");
    }
    if (value.is_object()) {
      std::vector<std::string> values;
      for (const auto& [field, member] : value.items())
        values.push_back(fieldName(field) + ": " + auditValue(field, member, lookup));
      return values.empty() ? "None" : join(values, "\n");
    }
    return value.dump();
  }
  std::vector<AuditDetail> auditEntryDetails(const Json& entry, const AuditLookup& lookup) {
    std::vector<AuditDetail> details;
    const auto changes = entry.find("changes");
    if (changes != entry.end() && changes->is_array())
      for (const auto& change : *changes) {
        const auto key = text(change, "key");
        if (key.empty())
          continue;
        const bool before = change.contains("old_value"), after = change.contains("new_value");
        const Json empty;
        if (key == "permissions" || key == "allow" || key == "deny") {
          const auto oldBits = bits(before ? change["old_value"] : empty);
          const auto newBits = bits(after ? change["new_value"] : empty);
          std::vector<unsigned> all;
          std::ranges::set_union(oldBits, newBits, std::back_inserter(all));
          for (const auto bit : all) {
            const bool was = std::ranges::find(oldBits, bit) != oldBits.end(),
                       is = std::ranges::find(newBits, bit) != newBits.end();
            if (was != is)
              details.push_back(
                  {fieldName(key) + " · " + permissionName(bit), was ? "Enabled" : "Disabled",
                   is ? "Enabled" : "Disabled", true, true}
              );
          }
          if (oldBits != newBits)
            continue;
        }
        if (key == "$remove")
          details.push_back(
              {fieldName(key), auditValue(key, after ? change["new_value"] : empty, lookup), "Removed", true, true}
          );
        else
          details.push_back(
              {fieldName(key), before ? auditValue(key, change["old_value"], lookup) : "Not set",
               after ? auditValue(key, change["new_value"], lookup) : "Not set", before, after}
          );
      }
    if (entry.contains("options") && entry["options"].is_object()) {
      static const std::map<std::string, std::string> options = {
          {"channel_id", "Channel"},
          {"message_id", "Message ID"},
          {"count", "Affected items"},
          {"delete_member_days", "Days inactive"},
          {"members_removed", "Members removed"},
          {"id", "Permission target"},
          {"role_name", "Role"},
          {"type", "Permission target type"},
          {"application_id", "Application"},
          {"auto_moderation_rule_name", "AutoMod rule"},
          {"auto_moderation_rule_trigger_type", "AutoMod trigger"},
          {"integration_type", "Integration"},
          {"status", "Voice status"}
      };
      for (const auto& [key, value] : entry["options"].items()) {
        const auto at = options.find(key);
        auto formatted = auditValue(key, value, lookup);
        if (key == "type")
          formatted = value == "0" ? "Role" : value == "1" ? "Member" : formatted;
        details.push_back({at == options.end() ? title(key) : at->second, "", std::move(formatted), false, true});
      }
    }
    return details;
  }
  void ServerAuditState::replace(const Json& response, bool append) {
    references.merge(response);
    if (!append)
      entries = Json::array();
    const auto records = response.is_object() ? response.find("audit_log_entries") : response.end();
    if (records == response.end() || !records->is_array())
      return;
    for (const auto& entry : *records) {
      const auto id = auditEntryId(entry);
      if (id.empty())
        continue;
      const auto old = std::ranges::find_if(entries, [&](const auto& value) { return auditEntryId(value) == id; });
      if (old == entries.end())
        entries.push_back(entry);
      else
        *old = entry;
    }
    const auto newer = [](const auto& a, const auto& b) {
      const auto x = auditEntryId(a), y = auditEntryId(b);
      return x.size() == y.size() ? x > y : x.size() > y.size();
    };
    std::sort(entries.begin(), entries.end(), newer);
  }
} // namespace app
