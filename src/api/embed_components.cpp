#include "api/embed_components.h"

#include "api/channel_features.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace api {
  namespace {
    std::string string(const nlohmann::json& value, const char* key, std::size_t limit) {
      const auto at = value.find(key);
      return at != value.end() && at->is_string() && at->get_ref<const std::string&>().size() <= limit
          ? at->get<std::string>()
          : std::string{};
    }
    bool boolean(const nlohmann::json& value, const char* key, bool fallback = false) {
      const auto at = value.find(key);
      return at != value.end() && at->is_boolean() ? at->get<bool>() : fallback;
    }
    int integer(const nlohmann::json& value, const char* key, int fallback, int maximum) {
      const auto at = value.find(key);
      return at != value.end() && at->is_number_integer() && *at >= 0 && *at <= maximum ? at->get<int>() : fallback;
    }
    std::optional<EmbedComponentMedia> media(const nlohmann::json& value) {
      const auto at = value.find("media");
      if (at == value.end() || !at->is_object())
        return {};
      auto url = mediaUrl(string(*at, "proxy_url", 4096));
      if (url.empty())
        url = mediaUrl(string(*at, "url", 4096));
      if (url.empty())
        return {};
      return EmbedComponentMedia{
          {url, integer(*at, "width", 0, 100000), integer(*at, "height", 0, 100000)},
          string(value, "description", 1024),
          boolean(value, "spoiler"),
          string(*at, "content_type", 128).starts_with("video/")
      };
    }
    std::optional<EmbedComponentLink> link(const nlohmann::json& value) {
      if (integer(value, "type", 0, 255) != 2 || integer(value, "style", 0, 6) != 5)
        return {};
      auto url = string(value, "url", 2048), label = string(value, "label", 320);
      if (!externalLink(url))
        return {};
      if (label.empty() && value.contains("emoji") && value["emoji"].is_object())
        label = string(value["emoji"], "name", 128);
      return EmbedComponentLink{label.empty() ? "Open link" : label, url, boolean(value, "disabled")};
    }
  } // namespace
  std::vector<EmbedComponent> parseEmbedComponents(const nlohmann::json& value) {
    unsigned visited = 0, pictures = 0;
    auto parse = [&](auto&& self, const nlohmann::json& node, unsigned depth) -> std::optional<EmbedComponent> {
      if (depth > 6 || visited >= 40 || !node.is_object())
        return {};
      ++visited;
      EmbedComponent result;
      result.type = integer(node, "type", 0, 255);
      result.spoiler = boolean(node, "spoiler");
      switch (result.type) {
      case 1:
      case 9:
      case 17:
        if (const auto at = node.find("components"); at != node.end() && at->is_array())
          for (const auto& child : *at) {
            if (visited >= 40)
              break;
            if (auto parsed = self(self, child, depth + 1); parsed && (result.type != 1 || parsed->type == 2))
              result.children.push_back(std::move(*parsed));
          }
        if (result.type == 9)
          if (const auto at = node.find("accessory"); at != node.end() && at->is_object() && visited < 40) {
            ++visited;
            if (integer(*at, "type", 0, 255) == 11)
              result.thumbnail = media(*at);
            else
              result.link = link(*at);
          }
        if (result.type == 17)
          if (const auto at = node.find("accent_color");
              at != node.end() && at->is_number_integer() && *at >= 0 && *at <= 0xFFFFFF)
            result.accent = at->get<std::uint32_t>();
        if (result.children.empty() && !result.thumbnail && !result.link)
          return {};
        break;
      case 2:
        result.link = link(node);
        if (!result.link)
          return {};
        break;
      case 10:
        result.content = string(node, "content", 16000);
        if (result.content.empty())
          return {};
        break;
      case 11:
        result.thumbnail = media(node);
        if (!result.thumbnail)
          return {};
        break;
      case 12:
        if (const auto at = node.find("items"); at != node.end() && at->is_array())
          for (const auto& item : *at) {
            if (pictures >= 10)
              break;
            ++pictures;
            if (item.is_object())
              if (auto parsed = media(item))
                result.gallery.push_back(std::move(*parsed));
          }
        if (result.gallery.empty())
          return {};
        break;
      case 14:
        result.spacing = integer(node, "spacing", 1, 2) == 2 ? 2 : 1;
        result.divider = boolean(node, "divider", true);
        break;
      default:
        return {};
      }
      return result;
    };
    std::vector<EmbedComponent> result;
    if (value.is_array())
      for (const auto& node : value) {
        if (visited >= 40)
          break;
        if (auto parsed = parse(parse, node, 0))
          result.push_back(std::move(*parsed));
      }
    return result;
  }
} // namespace api
