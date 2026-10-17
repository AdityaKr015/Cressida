#include "discord/gifs.h"

#include "discord/models.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace discord {
  namespace {
    bool shareUrl(const std::string& url) {
      return url.starts_with("https://")
          && url.size() <= 2000
          && url.size() > 8
          && std::ranges::none_of(url, [](unsigned char c) { return c <= 32 || c == 127 || c == '\\'; });
    }
    std::string text(const nlohmann::json& value, const char* key) {
      auto found = value.find(key);
      return found != value.end() && found->is_string() ? found->get<std::string>() : std::string{};
    }
    int dimension(const nlohmann::json& value, const char* key) {
      auto found = value.find(key);
      return found != value.end() && found->is_number_integer() && *found > 0 && *found <= 16384 ? found->get<int>()
                                                                                                 : 0;
    }
  } // namespace
  bool validGif(const Gif& gif) {
    return shareUrl(gif.url)
        && !mediaUrl(gif.src).empty()
        && gif.width >= 0
        && gif.width <= 16384
        && gif.height >= 0
        && gif.height <= 16384
        && (gif.format == 1 || gif.format == 2);
  }
  std::string gifSearchPath(const std::string& query) {
    if (query.size() > 256)
      throw std::runtime_error("GIF search is too long");
    const std::string options = "?media_format=gif&locale=en-US&limit=40";
    if (query.empty())
      return "/gifs/trending-gifs" + options;
    constexpr char hex[] = "0123456789ABCDEF";
    std::string escaped;
    for (unsigned char ch : query) {
      if ((ch >= 'a' && ch <= 'z')
          || (ch >= 'A' && ch <= 'Z')
          || (ch >= '0' && ch <= '9')
          || ch == '-'
          || ch == '_'
          || ch == '.'
          || ch == '~')
        escaped += char(ch);
      else {
        escaped += '%';
        escaped += hex[ch >> 4];
        escaped += hex[ch & 15];
      }
    }
    return "/gifs/search" + options + "&q=" + escaped;
  }
  Gif gifFromJson(const nlohmann::json& value) {
    if (!value.is_object())
      return {};
    Gif gif;
    gif.id = text(value, "id");
    gif.title = text(value, "title");
    gif.url = text(value, "url");
    gif.src = mediaUrl(text(value, "src"));
    gif.preview = mediaUrl(text(value, "preview"));
    if (gif.src.empty())
      gif.src = mediaUrl(text(value, "gif_src"));
    gif.width = dimension(value, "width");
    gif.height = dimension(value, "height");
    if (value.contains("format") && value["format"].is_number_unsigned())
      gif.format = value["format"].get<unsigned>();
    return gif;
  }
  nlohmann::json gifJson(const Gif& gif) {
    return {{"id", gif.id},           {"title", gif.title}, {"url", gif.url},       {"src", gif.src},
            {"preview", gif.preview}, {"width", gif.width}, {"height", gif.height}, {"format", gif.format}};
  }
  std::vector<Gif> parseGifs(const nlohmann::json& value) {
    const auto* items = &value;
    if (value.is_object() && value.contains("gifs"))
      items = &value["gifs"];
    if (!items->is_array())
      throw std::runtime_error("Unreadable GIF search");
    std::vector<Gif> result;
    for (const auto& item : *items) {
      if (result.size() >= 100)
        break;
      auto gif = gifFromJson(item);
      if (validGif(gif) && std::ranges::find(result, gif.url, &Gif::url) == result.end())
        result.push_back(std::move(gif));
    }
    return result;
  }
} // namespace discord
