#include "api/profile.h"

#include "api/role_styles.h"

#include <algorithm>
#include <charconv>
#include <ctime>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace api {
  namespace {
    using Json = nlohmann::json;
    std::string text(const Json& value, const char* key, std::size_t limit = 1024) {
      const auto at = value.find(key);
      return at != value.end() && at->is_string() && at->get_ref<const std::string&>().size() <= limit
          ? at->get<std::string>()
          : "";
    }
    bool hash(const std::string& value) { return validAssetHash(value); }
    std::optional<std::uint32_t> color(const Json& value) {
      if (!value.is_number_integer())
        return {};
      if (value.is_number_unsigned()) {
        const auto result = value.get<std::uint64_t>();
        return result <= 0xFFFFFF ? std::optional(static_cast<std::uint32_t>(result)) : std::nullopt;
      }
      const auto result = value.get<std::int64_t>();
      return result >= 0 && result <= 0xFFFFFF ? std::optional(static_cast<std::uint32_t>(result)) : std::nullopt;
    }
  } // namespace
  std::string accountCreationDate(std::string_view user) {
    if (!validSnowflake(std::string(user)))
      return {};
    constexpr std::uint64_t accountEpochMilliseconds = 1420070400000ULL;
    constexpr unsigned timestampBits = 22;
    std::uint64_t id = 0;
    const auto parsed = std::from_chars(user.data(), user.data() + user.size(), id);
    if (parsed.ec != std::errc{} || parsed.ptr != user.data() + user.size())
      return {};
    const auto seconds = static_cast<std::time_t>(((id >> timestampBits) + accountEpochMilliseconds) / 1000);
    std::tm utc{};
    if (!gmtime_r(&seconds, &utc))
      return {};
    constexpr std::size_t maximumDateBytes = 64;
    std::array<char, maximumDateBytes> buffer{};
    return std::strftime(buffer.data(), buffer.size(), "%d %b %Y", &utc) ? buffer.data() : "";
  }
  UserProfile parseUserProfile(const Json& value, const std::string& requestedGuild) {
    UserProfile result;
    result.user = parseAccount(value.at("user"));
    if (!validSnowflake(result.user.id))
      throw std::runtime_error("Invalid profile user");
    if (result.user.bio.size() > 4096)
      result.user.bio.clear();
    if (result.user.pronouns.size() > 160)
      result.user.pronouns.clear();
    const auto& user = value.at("user");
    result.privateProfile = value.contains("private") && value["private"] == true;
    if (user.contains("accent_color"))
      result.accent = color(user["accent_color"]);
    auto banner = [&](const Json& metadata, bool guild) {
      const auto id = text(metadata, "banner", 64);
      if (hash(id))
        result.banner = (guild ? "/guilds/" + result.guild + "/users/" + result.user.id + "/banners/"
                               : "/banners/" + result.user.id + "/")
            + id
            + (id.starts_with("a_") ? ".gif?size=512" : ".png?size=512");
    };
    auto metadata = [&](const Json& profile, bool guild) {
      if (!profile.is_object())
        return;
      if (profile.contains("bio") && profile["bio"].is_string())
        result.user.bio = text(profile, "bio", 4096);
      if (profile.contains("pronouns") && profile["pronouns"].is_string())
        result.user.pronouns = text(profile, "pronouns", 160);
      banner(profile, guild);
      if (!guild && profile.contains("accent_color"))
        if (auto parsed = color(profile["accent_color"]))
          result.accent = parsed;
      if (profile.contains("theme_colors")
          && profile["theme_colors"].is_array()
          && profile["theme_colors"].size() == 2) {
        const auto first = color(profile["theme_colors"][0]), second = color(profile["theme_colors"][1]);
        if (first && second)
          result.theme = std::array{*first, *second};
      }
    };
    banner(user, false);
    if (value.contains("user_profile"))
      metadata(value["user_profile"], false);
    if (user.contains("avatar_decoration_data") && user["avatar_decoration_data"].is_object()) {
      const auto asset = text(user["avatar_decoration_data"], "asset", 64);
      if (hash(asset))
        result.decoration = "/avatar-decoration-presets/" + asset + ".png?size=160";
    }
    if (result.user.guildTag) {
      result.guildTag = result.user.guildTag->tag;
      result.guildBadge = result.user.guildTag->badge;
    }
    if (validSnowflake(requestedGuild)) {
      const auto profile = value.find("guild_member_profile");
      const bool matching = profile == value.end()
          || !profile->is_object()
          || !profile->contains("guild_id")
          || text(*profile, "guild_id") == requestedGuild;
      if (matching) {
        result.guild = requestedGuild;
        if (profile != value.end())
          metadata(*profile, true);
        if (value.contains("guild_member") && value["guild_member"].is_object()) {
          const auto& member = value["guild_member"];
          result.roles = parseMemberRoles(member);
          result.joinedAt = text(member, "joined_at", 64);
          const auto avatar = text(member, "avatar", 64);
          if (hash(avatar))
            result.user.avatar =
                "/guilds/" + result.guild + "/users/" + result.user.id + "/avatars/" + avatar + ".png?size=128";
          const auto nick = text(member, "nick", 128);
          if (!nick.empty())
            result.user.name = nick;
        }
      }
    }
    auto badges = [&](const char* key) {
      const auto at = value.find(key);
      if (at == value.end() || !at->is_array())
        return;
      for (const auto& badge : *at) {
        const auto icon = text(badge, "icon", 64);
        if (result.badges.size() >= 16)
          break;
        if (hash(icon))
          result.badges.push_back({text(badge, "description", 512), "/badge-icons/" + icon + ".png?size=32"});
      }
    };
    badges("badges");
    if (!result.guild.empty())
      badges("guild_badges");
    if (value.contains("mutual_friends_count") && value["mutual_friends_count"].is_number_unsigned())
      result.mutualFriendCount =
          static_cast<int>(std::min<std::uint64_t>(value["mutual_friends_count"].get<std::uint64_t>(), 1000000));
    else if (value.contains("mutual_friends_count") && value["mutual_friends_count"].is_number_integer())
      result.mutualFriendCount =
          static_cast<int>(std::clamp<std::int64_t>(value["mutual_friends_count"].get<std::int64_t>(), 0, 1000000));
    if (value.contains("mutual_friends") && value["mutual_friends"].is_array())
      for (const auto& friendUser : value["mutual_friends"]) {
        if (result.mutualFriends.size() >= 3)
          break;
        if (validSnowflake(text(friendUser, "id", 20)))
          result.mutualFriends.push_back(parseAccount(friendUser));
      }
    if (value.contains("mutual_guilds") && value["mutual_guilds"].is_array()) {
      std::set<std::string> guilds;
      for (const auto& guild : value["mutual_guilds"])
        if (guilds.size() < 1000 && validSnowflake(text(guild, "id", 20)))
          guilds.insert(text(guild, "id", 20));
      result.mutualGuildCount = static_cast<int>(guilds.size());
    }
    if (result.privateProfile) {
      result.user.bio.clear();
      result.user.pronouns.clear();
      result.badges.clear();
    }
    return result;
  }
  GuildProfile parseGuildProfile(const Json& value) {
    GuildProfile result;
    result.id = text(value, "id", 20);
    if (!validSnowflake(result.id))
      throw std::runtime_error("Invalid server profile identity");
    result.name = text(value, "name", 400);
    result.description = text(value, "description", 1600);
    auto asset = [&](const char* field, const char* route, int size) {
      const auto image = text(value, field, 64);
      return hash(image) ? std::string(route) + result.id + "/" + image + ".png?size=" + std::to_string(size) : "";
    };
    result.icon = asset("icon_hash", "/icons/", 128);
    result.banner = asset("custom_banner_hash", "/discovery-splashes/", 512);
    if (result.banner.empty())
      result.banner = asset("banner_hash", "/clan-banners/", 512);
    const auto badge = text(value, "badge_hash", 64), tag = text(value, "tag", 32);
    if (!tag.empty())
      result.tag =
          GuildTag{result.id, tag, hash(badge) ? "/guild-tag-badges/" + result.id + "/" + badge + ".png?size=32" : ""};
    auto count = [&](const char* key) -> std::optional<int> {
      const auto at = value.find(key);
      if (at == value.end() || !at->is_number_integer())
        return {};
      if (at->is_number_unsigned())
        return static_cast<int>(std::min<std::uint64_t>(at->get<std::uint64_t>(), 1000000000));
      const auto n = at->get<std::int64_t>();
      return n >= 0 ? std::optional(static_cast<int>(std::min<std::int64_t>(n, 1000000000))) : std::nullopt;
    };
    result.members = count("member_count");
    result.online = count("online_count");
    const auto brand = text(value, "brand_color_primary", 7);
    if (brand.size() == 7 && brand.front() == '#') {
      std::uint32_t number = 0;
      const auto parsed = std::from_chars(brand.data() + 1, brand.data() + brand.size(), number, 16);
      if (parsed.ec == std::errc{} && parsed.ptr == brand.data() + brand.size())
        result.accent = number;
    }
    if (value.contains("features") && value["features"].is_array())
      for (const auto& feature : value["features"]) {
        if (feature == "DISCOVERABLE")
          result.joinable = true;
        if (feature == "VERIFIED")
          result.verified = true;
        if (feature == "PARTNERED")
          result.partnered = true;
      }
    result.recruitment = value.contains("visibility") && value["visibility"] == 3;
    result.joinable =
        result.joinable || (value.contains("visibility") && value["visibility"] == 1) || result.recruitment;
    if (value.contains("traits") && value["traits"].is_array())
      for (const auto& trait : value["traits"]) {
        if (result.traits.size() >= 5)
          break;
        const auto name = text(trait, "label", 160);
        if (!name.empty())
          result.traits.push_back({name, text(trait, "emoji_name", 64)});
      }
    return result;
  }
} // namespace api
