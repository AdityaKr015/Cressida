#include "discord/models.h"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

namespace discord {
  namespace {
    // Read bounded wire fields without a generated schema. Organization remains
    // read-only; explicit presence edits preserve raw unknown status fields.
    class Wire {
    public:
      explicit Wire(std::string_view input) : bytes(input) {}
      std::string_view bytes;
      unsigned field = 0, type = 0;
      std::uint64_t number = 0;
      std::string_view data;
      std::uint64_t varint() {
        std::uint64_t result = 0;
        for (int shift = 0; shift < 70; shift += 7) {
          if (bytes.empty())
            throw std::runtime_error("Truncated settings");
          const auto byte = static_cast<unsigned char>(bytes.front());
          bytes.remove_prefix(1);
          if (shift == 63 && byte > 1)
            throw std::runtime_error("Settings integer overflow");
          result |= std::uint64_t(byte & 127) << shift;
          if (!(byte & 128))
            return result;
        }
        throw std::runtime_error("Invalid settings integer");
      }
      std::string_view take(std::uint64_t count) {
        if (count > bytes.size())
          throw std::runtime_error("Truncated settings field");
        const auto result = bytes.substr(0, count);
        bytes.remove_prefix(count);
        return result;
      }
      bool next() {
        if (bytes.empty())
          return false;
        const auto tag = varint();
        field = tag >> 3;
        type = tag & 7;
        number = 0;
        data = {};
        if (!field)
          throw std::runtime_error("Invalid settings field");
        if (type == 0)
          number = varint();
        else if (type == 1 || type == 5) {
          data = take(type == 1 ? 8 : 4);
          for (std::size_t i = 0; i < data.size(); ++i)
            number |= std::uint64_t(static_cast<unsigned char>(data[i])) << (i * 8);
        } else if (type == 2)
          data = take(varint());
        else
          throw std::runtime_error("Unsupported settings wire type");
        return true;
      }
    };

    std::string base64(std::string_view input) {
      if (input.size() > 16 * 1024 * 1024)
        throw std::runtime_error("Settings too large");
      constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      std::string output;
      std::uint32_t bits = 0;
      int count = 0;
      bool padding = false;
      for (const auto ch : input) {
        if (ch == '=') {
          padding = true;
          continue;
        }
        const auto value = alphabet.find(ch);
        if (padding || value == std::string_view::npos)
          throw std::runtime_error("Invalid settings encoding");
        bits = (bits << 6) | static_cast<unsigned int>(value);
        count += 6;
        if (count >= 8) {
          count -= 8;
          output += static_cast<char>((bits >> count) & 255);
        }
      }
      return output;
    }

    void ids(const Wire& field, std::vector<std::string>& output) {
      if (field.type == 1)
        output.push_back(std::to_string(field.number));
      else if (field.type == 2) {
        Wire packed(field.data);
        while (!packed.bytes.empty()) {
          const auto bytes = packed.take(8);
          std::uint64_t number = 0;
          for (int i = 0; i < 8; ++i)
            number |= std::uint64_t(static_cast<unsigned char>(bytes[i])) << (i * 8);
          output.push_back(std::to_string(number));
        }
      }
    }
  } // namespace

  bool validStatus(std::string_view status) {
    return status == "online" || status == "idle" || status == "dnd" || status == "invisible";
  }
  namespace {
    std::string encodedSettings(const nlohmann::json& value) {
      return value.value("settings", value.value("proto", std::string{}));
    }
    void integer(std::string& output, std::uint64_t value) {
      while (value >= 128) {
        output += static_cast<char>((value & 127) | 128);
        value >>= 7;
      }
      output += static_cast<char>(value);
    }
    std::string field(unsigned id, std::string_view value) {
      std::string output;
      integer(output, (id << 3) | 2);
      integer(output, value.size());
      output += value;
      return output;
    }
    std::string encode64(std::string_view bytes) {
      constexpr std::string_view chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      std::string result;
      for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0;
        const auto c = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0;
        result += chars[a >> 2];
        result += chars[((a & 3) << 4) | (b >> 4)];
        result += i + 1 < bytes.size() ? chars[((b & 15) << 2) | (c >> 6)] : '=';
        result += i + 2 < bytes.size() ? chars[c & 63] : '=';
      }
      return result;
    }
  } // namespace
  std::optional<Presence> parsePresenceSettings(const nlohmann::json& value) {
    auto bytes = base64(encodedSettings(value));
    Wire root(bytes);
    while (root.next())
      if (root.field == 11 && root.type == 2) {
        Presence result;
        Wire status(root.data);
        while (status.next()) {
          if (status.field == 1 && status.type == 2) {
            Wire wrapped(status.data);
            while (wrapped.next())
              if (wrapped.field == 1 && wrapped.type == 2)
                result.status = wrapped.data;
          }
          if (status.field == 2 && status.type == 2) {
            Wire custom(status.data);
            while (custom.next()) {
              if (custom.field == 1 && custom.type == 2)
                result.text = custom.data;
              if (custom.field == 2 && custom.type == 1 && custom.number)
                result.emojiId = std::to_string(custom.number);
              if (custom.field == 3 && custom.type == 2)
                result.emojiName = custom.data;
            }
          }
        }
        return result;
      }
    return {};
  }
  nlohmann::json presenceSettingsPatch(
      const nlohmann::json& current, const std::string& status, const std::optional<std::string>& customText
  ) {
    if (!validStatus(status) || (customText && utf8Length(*customText) > 128))
      throw std::runtime_error("Invalid presence");
    const auto bytes = base64(encodedSettings(current));
    Wire root(bytes);
    std::string block;
    std::optional<std::uint64_t> version;
    while (root.next()) {
      if (root.field == 1 && root.type == 2) {
        Wire versions(root.data);
        while (versions.next())
          if (versions.field == 3 && versions.type == 0)
            version = versions.number;
      }
      if (root.field != 11 || root.type != 2)
        continue;
      Wire settings(root.data);
      while (!settings.bytes.empty()) {
        const auto before = settings.bytes;
        settings.next();
        if (settings.field != 1 && !(customText && settings.field == 2))
          block += before.substr(0, before.size() - settings.bytes.size());
      }
    }
    block += field(1, field(1, status));
    if (customText)
      block += field(2, customText->empty() ? std::string{} : field(1, *customText));
    nlohmann::json result{{"settings", encode64(field(11, block))}};
    if (version)
      result["required_data_version"] = *version;
    return result;
  }

  namespace {
    struct SettingField {
      const char* key;
      unsigned group, id;
      bool wrapped;
      int fallback, maximum;
    };
    constexpr SettingField fields[] = {
        {"restrict_new_guild_dms", 8, 27, true, 0, 1},
        {"restrict_message_requests", 8, 17, true, 0, 1},
        {"friend_sources", 8, 11, true, 14, 14},
        {"contact_sync", 8, 10, true, 0, 1},
        {"hide_legacy_username", 8, 22, true, 0, 1},
        {"conversation_warnings", 8, 23, true, 1, 1},
        {"recent_games", 8, 24, true, 1, 1},
        {"profile_visibility", 8, 32, false, 0, 3},
        {"join_friends", 8, 1, true, 1, 1},
        {"join_voice", 8, 2, true, 1, 1},
        {"show_activity", 11, 3, true, 1, 1},
        {"in_app_notifications", 7, 1, true, 1, 1},
        {"notify_friends_stream", 7, 2, true, 0, 1},
        {"friend_online_notifications", 7, 12, true, 1, 1},
        {"profile_notifications", 7, 16, true, 1, 1},
        {"server_event_notifications", 7, 22, true, 1, 1},
        {"notify_friends_profile", 7, 24, true, 1, 1},
        {"notify_friends_online", 7, 25, true, 1, 1},
        {"reaction_notifications", 7, 7, false, 0, 2},
        {"dm_spam_filter", 6, 27, false, 0, 3},
    };
    const SettingField* settingField(std::string_view key) {
      for (const auto& f : fields)
        if (f.key == key)
          return &f;
      return nullptr;
    }
    std::string numericField(unsigned id, std::uint64_t value) {
      std::string output;
      integer(output, id << 3);
      integer(output, value);
      return output;
    }
  } // namespace
  bool validSettingChanges(const SettingValues& changes) {
    if (changes.empty() || changes.size() > std::size(fields))
      return false;
    for (const auto& [key, value] : changes) {
      const auto* f = settingField(key);
      if (!f || value < 0 || value > f->maximum)
        return false;
      if (key == "friend_sources" && (value & ~14 || ((value & 8) && (value & 6) != 6)))
        return false;
    }
    return true;
  }
  SettingValues parseAccountSettings(const nlohmann::json& value) {
    SettingValues result;
    for (const auto& f : fields)
      result[f.key] = f.fallback;
    const auto bytes = base64(encodedSettings(value));
    Wire root(bytes);
    while (root.next()) {
      if (root.type != 2 || (root.field != 6 && root.field != 7 && root.field != 8 && root.field != 11))
        continue;
      Wire group(root.data);
      while (group.next())
        for (const auto& f : fields) {
          if (f.group != root.field || f.id != group.field)
            continue;
          std::uint64_t number = f.fallback;
          if (!f.wrapped && group.type == 0)
            number = group.number;
          else if (f.wrapped && group.type == 2) {
            number = 0;
            Wire wrapper(group.data);
            while (wrapper.next())
              if (wrapper.field == 1 && wrapper.type == 0)
                number = wrapper.number;
          }
          if (number <= static_cast<std::uint64_t>(f.maximum))
            result[f.key] = number;
        }
    }
    return result;
  }
  nlohmann::json accountSettingsPatch(const nlohmann::json& snapshot, const SettingValues& changes) {
    if (!validSettingChanges(changes))
      throw std::runtime_error("Invalid settings edits");
    std::map<unsigned, std::map<unsigned, std::string>> replacements;
    for (const auto& [key, value] : changes) {
      const auto& f = *settingField(key);
      replacements[f.group][f.id] = f.wrapped ? field(f.id, numericField(1, value)) : numericField(f.id, value);
    }
    const auto bytes = base64(encodedSettings(snapshot));
    Wire root(bytes);
    std::map<unsigned, std::string> groups;
    std::optional<std::uint64_t> version;
    while (root.next()) {
      if (root.field == 1 && root.type == 2) {
        Wire versions(root.data);
        while (versions.next())
          if (versions.field == 3 && versions.type == 0)
            version = versions.number;
      }
      if (root.type != 2 || !replacements.contains(root.field))
        continue;
      Wire group(root.data);
      while (!group.bytes.empty()) {
        const auto before = group.bytes;
        group.next();
        if (!replacements[root.field].contains(group.field))
          groups[root.field] += before.substr(0, before.size() - group.bytes.size());
      }
    }
    if (!version)
      throw std::runtime_error("Cannot save settings without an account data version");
    std::string edited;
    for (const auto& [group, updates] : replacements) {
      auto& bytes = groups[group];
      for (const auto& [_, replacement] : updates)
        bytes += replacement;
      edited += field(group, bytes);
    }
    nlohmann::json result{{"settings", encode64(edited)}};
    if (version)
      result["required_data_version"] = *version;
    return result;
  }

  GuildLayout parseGuildLayout(const nlohmann::json& value) {
    GuildLayout result;
    if (value.contains("guild_folders") && value["guild_folders"].is_array()) {
      for (const auto& item : value["guild_folders"]) {
        GuildFolder folder;
        if (item.contains("id") && !item["id"].is_null())
          folder.id = item["id"].is_string() ? item["id"].get<std::string>() : item["id"].dump();
        if (item.contains("name") && item["name"].is_string())
          folder.name = item["name"];
        if (item.contains("color") && item["color"].is_number_unsigned())
          folder.color = item["color"];
        if (item.contains("guild_ids"))
          for (const auto& id : item["guild_ids"])
            if (id.is_string() && validSnowflake(id.get<std::string>()))
              folder.guildIds.push_back(id);
        result.folders.push_back(std::move(folder));
      }
      if (value.contains("guild_positions"))
        for (const auto& id : value["guild_positions"])
          if (id.is_string() && validSnowflake(id.get<std::string>()))
            result.positions.push_back(id);
      return result;
    }
    const auto encoded = value.value("settings", value.value("proto", std::string{}));
    const auto decoded = base64(encoded);
    Wire root(decoded);
    while (root.next()) {
      if (root.field != 14 || root.type != 2)
        continue;
      Wire folders(root.data);
      while (folders.next()) {
        if (folders.field == 2)
          ids(folders, result.positions);
        if (folders.field != 1 || folders.type != 2)
          continue;
        GuildFolder folder;
        Wire entry(folders.data);
        while (entry.next()) {
          if (entry.field == 1)
            ids(entry, folder.guildIds);
          if (entry.type == 2 && entry.field >= 2 && entry.field <= 4) {
            Wire wrapper(entry.data);
            // An empty wrapper still means folder ID 0 is present.
            if (entry.field == 2)
              folder.id = "0";
            while (wrapper.next())
              if (wrapper.field == 1) {
                if (entry.field == 2 && wrapper.type == 0)
                  folder.id = std::to_string(wrapper.number);
                if (entry.field == 3 && wrapper.type == 2)
                  folder.name = wrapper.data;
                if (entry.field == 4 && wrapper.type == 0)
                  folder.color = wrapper.number & 0xFFFFFF;
              }
          }
        }
        result.folders.push_back(std::move(folder));
      }
    }
    return result;
  }
} // namespace discord
