#include "app/server_commands.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <set>
#include <stdexcept>

namespace app {
  using Json = nlohmann::json;
  namespace {
    bool id(const std::string& value) {
      std::uint64_t number = 0;
      const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
      return !value.empty()
          && value.front() != '0'
          && number
          && parsed.ec == std::errc{}
      && parsed.ptr == value.data() + value.size();
    }
    std::map<std::string, Json> records(const Json& permissions) {
      if (!permissions.is_array() || permissions.size() > 100)
        throw std::runtime_error("A command can have at most 100 permission overrides.");
      std::map<std::string, Json> result;
      for (const auto& record : permissions) {
        const auto key = commandPermissionKey(record);
        if (key.empty() || record.size() != 3 || !result.emplace(key, record).second)
          throw std::runtime_error("The server returned an unsupported or duplicate command permission override.");
      }
      return result;
    }
  } // namespace
  std::string commandPermissionKey(const Json& permission) {
    if (!permission.is_object()
        || !permission.contains("id")
        || !permission["id"].is_string()
        || !id(permission["id"].get<std::string>())
        || !permission.contains("type")
        || !permission["type"].is_number_integer()
        || permission["type"] < 1
        || permission["type"] > 3
        || !permission.contains("permission")
        || !permission["permission"].is_boolean())
      return {};
    return std::to_string(permission["type"].get<int>()) + ":" + permission["id"].get<std::string>();
  }
  std::string allCommandChannelsId(std::string guild) {
    if (!id(guild))
      return {};
    for (std::size_t at = guild.size(); at; --at) {
      if (guild[at - 1] != '0') {
        --guild[at - 1];
        break;
      }
      guild[at - 1] = '9';
    }
    const auto nonzero = guild.find_first_not_of('0');
    return nonzero == std::string::npos ? "0" : guild.substr(nonzero);
  }
  Json editCommandPermission(Json permissions, const std::string& target, int type, std::optional<bool> allow) {
    auto valid = records(permissions);
    if (!id(target) || type < 1 || type > 3)
      throw std::runtime_error("Choose a valid role, member or channel before adding an override.");
    const auto key = std::to_string(type) + ":" + target;
    if (!allow)
      valid.erase(key);
    else
      valid[key] = {{"id", target}, {"type", type}, {"permission", *allow}};
    if (valid.size() > 100)
      throw std::runtime_error(
          "A command can have at most 100 permission overrides. Remove an override before adding another."
      );
    auto result = Json::array();
    for (const auto& [key, record] : valid)
      result.push_back(record);
    return result;
  }
  Json mergeCommandPermissionEdits(
      const Json& current, const Json& baseline, const Json& draft, bool restoreApplicationDefaults
  ) {
    auto fresh = records(current);
    const auto original = records(baseline), edited = records(draft);
    if (restoreApplicationDefaults)
      return Json::array();
    std::set<std::string> keys;
    for (const auto& [key, value] : original)
      keys.insert(key);
    for (const auto& [key, value] : edited)
      keys.insert(key);
    for (const auto& key : keys) {
      const auto before = original.find(key), after = edited.find(key);
      if (before != original.end() && after != edited.end() && before->second == after->second)
        continue;
      if (after == edited.end())
        fresh.erase(key);
      else
        fresh[key] = after->second;
    }
    if (fresh.size() > 100)
      throw std::runtime_error(
          "The server now has too many command overrides to apply these changes. Reload and try again."
      );
    auto result = Json::array();
    for (const auto& [key, value] : fresh)
      result.push_back(value);
    return result;
  }
} // namespace app
