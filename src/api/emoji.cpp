#include "api/emoji.h"

#include "api/models.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <set>

namespace api {
  bool emojiNeedsImage(const Emoji& emoji, const std::string& guild, int premiumType) {
    return !emoji.id.empty() && (!emoji.available || (premiumType <= 0 && (emoji.animated || emoji.guild != guild)));
  }
  std::optional<Emoji> emojiFromToken(std::string_view token) {
    if ((!token.starts_with("<:") && !token.starts_with("<a:")) || !token.ends_with('>') || token.size() > 100)
      return {};
    const auto begin = token.starts_with("<a:") ? 3U : 2U;
    const auto colon = token.find(':', begin);
    if (colon == std::string_view::npos || colon == begin)
      return {};
    const auto name = token.substr(begin, colon - begin);
    const auto id = token.substr(colon + 1, token.size() - colon - 2);
    if (name.size() > 64 || !validSnowflake(std::string(id)) || !std::ranges::all_of(name, [](unsigned char ch) {
          return std::isalnum(ch) || ch == '_';
        }))
      return {};
    Emoji emoji;
    emoji.name = name;
    emoji.id = id;
    emoji.animated = begin == 3;
    return emoji;
  }
  std::string emojiImageLink(const Emoji& emoji) {
    if (!validSnowflake(emoji.id) || emoji.name.empty() || std::ranges::any_of(emoji.name, [](unsigned char ch) {
          return !(std::isalnum(ch) || ch == '_');
        }))
      return emoji.token();
    return "["
        + emoji.name
        + "](https://cdn.discordapp.com/emojis/"
        + emoji.id
        + (emoji.animated ? ".gif" : ".png")
        + "?size=48&name="
        + emoji.name
        + "&lossless=true)";
  }
  std::optional<Emoji> emojiFromImageLink(std::string_view url) {
    constexpr std::string_view origins[] = {
        "https://cdn.discordapp.com/emojis/", "https://media.discordapp.net/emojis/"
    };
    for (const auto origin : origins) {
      if (!url.starts_with(origin))
        continue;
      auto path = url.substr(origin.size());
      const auto dot = path.find('.');
      if (dot == std::string_view::npos || !validSnowflake(std::string(path.substr(0, dot))))
        return {};
      const auto query = path.find('?');
      const auto ext = path.substr(dot, query == std::string_view::npos ? path.size() - dot : query - dot);
      if (ext != ".gif" && ext != ".png" && ext != ".webp")
        return {};
      Emoji emoji;
      emoji.id = path.substr(0, dot);
      emoji.name = "emoji";
      emoji.animated = ext == ".gif";
      if (query != std::string_view::npos) {
        auto params = path.substr(query + 1);
        while (!params.empty()) {
          const auto amp = params.find('&');
          const auto part = params.substr(0, amp);
          if (part == "animated=true")
            emoji.animated = true;
          if (part.starts_with("name=")) {
            const auto name = part.substr(5);
            if (!name.empty() && name.size() <= 64 && std::ranges::all_of(name, [](unsigned char ch) {
                  return std::isalnum(ch) || ch == '_';
                }))
              emoji.name = name;
          }
          if (amp == std::string_view::npos)
            break;
          params.remove_prefix(amp + 1);
        }
      }
      return emoji;
    }
    return {};
  }
  bool validEmojiKey(const std::string& key) {
    return !key.empty()
        && key.size() <= 160
        && std::ranges::none_of(key, [](unsigned char ch) { return ch <= 32 || ch == 127; });
  }
  std::vector<Emoji> parseGuildEmojis(const nlohmann::json& value, const std::string& guild) {
    std::vector<Emoji> result;
    if (!validSnowflake(guild) || !value.is_array())
      return result;
    std::set<std::string> seen;
    for (const auto& item : value) {
      if (result.size() >= 1000)
        break;
      if (!item.is_object()
          || !item.contains("id")
          || !item["id"].is_string()
          || !item.contains("name")
          || !item["name"].is_string())
        continue;
      Emoji emoji;
      emoji.id = item["id"].get<std::string>();
      emoji.name = item["name"].get<std::string>();
      emoji.guild = guild;
      if (!validSnowflake(emoji.id)
          || emoji.name.empty()
          || emoji.name.size() > 64
          || std::ranges::any_of(emoji.name, [](unsigned char ch) { return !(std::isalnum(ch) || ch == '_'); })
          || !seen.insert(emoji.id).second)
        continue;
      emoji.animated = item.value("animated", false);
      emoji.available = item.value("available", true);
      result.push_back(std::move(emoji));
    }
    return result;
  }
} // namespace api
