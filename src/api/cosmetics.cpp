#include "api/cosmetics.h"

#include "api/models.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <nlohmann/json.hpp>

namespace api {
  namespace {
    using Json = nlohmann::json;
    constexpr std::size_t maximumLayers = 16, maximumAssetBytes = 512, maximumCollectibles = 16;
    constexpr int maximumEffectZIndex = 1024;
    constexpr int maximumEffectMilliseconds = 300000, maximumFrameDimension = 2048;
    std::string text(const Json& value, const char* key) {
      const auto at = value.find(key);
      return at != value.end() && at->is_string() && at->get_ref<const std::string&>().size() <= maximumAssetBytes
          ? at->get<std::string>()
          : "";
    }
    bool component(std::string_view s) {
      return !s.empty() && s.size() <= maximumAssetBytes && std::ranges::all_of(s, [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
      });
    }
    std::string source(const Json& value, const char* key) {
      auto s = text(value, key);
      constexpr std::string_view origin = "https://cdn.discordapp.com";
      if (s.starts_with(origin))
        s.erase(0, origin.size());
      if ((!s.starts_with("/assets/profile_effects/")
           && !s.starts_with("/assets/content/")
           && !s.starts_with("/media/v1/collectibles-shop/"))
          || s.contains("..")
          || s.contains('%')
          || s.contains('\\')
          || s.contains('?')
          || s.contains('#'))
        return {};
      if (!std::ranges::all_of(s, [](unsigned char c) {
            return std::isalnum(c) || c == '/' || c == '_' || c == '-' || c == '.';
          }))
        return {};
      return s;
    }
    std::optional<float> number(const Json& value, const char* key, float limit) {
      const auto at = value.find(key);
      if (at == value.end() || !at->is_number())
        return {};
      const auto n = at->get<double>();
      return std::isfinite(n) && n >= 0 && n <= limit ? std::optional(static_cast<float>(n)) : std::nullopt;
    }
    std::chrono::milliseconds time(const Json& value, const char* key) {
      return std::chrono::milliseconds(static_cast<int>(number(value, key, maximumEffectMilliseconds).value_or(0)));
    }
  } // namespace
  std::string collectibleAsset(const std::string& sku, std::string_view id, bool animated) {
    if (!validSnowflake(sku) || (!id.empty() && !component(id)))
      return {};
    return "/media/v1/collectibles-shop/"
        + sku
        + "/"
        + (id.empty() ? "" : std::string(id) + "/")
        + (animated ? "animated" : "static");
  }
  std::string cosmeticAssetVariant(const std::string& path, bool animated) {
    if (path.starts_with("/media/v1/collectibles-shop/") && path.ends_with("/static"))
      return path.substr(0, path.size() - std::string_view("static").size()) + (animated ? "animated" : "static");
    if (path.starts_with("/avatar-decoration-presets/"))
      return path.substr(0, path.find('?')) + (animated ? "?size=160&passthrough=true" : "?size=160&passthrough=false");
    return path;
  }
  std::optional<std::uint32_t> nameplateBackground(std::string_view palette, bool light) {
    struct Palette {
      std::string_view name;
      std::uint32_t dark, light;
    };
    static constexpr Palette palettes[] = {{"crimson", 0x900007, 0xE7040F}, {"berry", 0x893A99, 0xB11FCF},
                                           {"sky", 0x0080B7, 0x56CCFF},     {"teal", 0x086460, 0x7DEED7},
                                           {"forest", 0x2D5401, 0x6AA624},  {"bubble_gum", 0xDC3E97, 0xF957B3},
                                           {"violet", 0x730BC8, 0x972FED},  {"cobalt", 0x0131C2, 0x4278FF},
                                           {"clover", 0x047B20, 0x63CD5A},  {"lemon", 0xF6CD12, 0xFED400},
                                           {"white", 0xFFFFFF, 0xFFFFFF},   {"black", 0x000000, 0x000000}};
    const auto at = std::ranges::find(palettes, palette, &Palette::name);
    return at == std::end(palettes) ? std::nullopt : std::optional(light ? at->light : at->dark);
  }
  UserCosmetics parseUserCosmetics(const Json& user) {
    UserCosmetics result;
    if (!user.is_object())
      return result;
    if (const auto at = user.find("avatar_decoration_data"); at != user.end()) {
      if (at->is_null())
        result.decoration = "";
      else if (at->is_object()) {
        const auto asset = text(*at, "asset"), sku = text(*at, "sku_id");
        if (validSnowflake(sku))
          result.decoration = collectibleAsset(sku);
        else if (validAssetHash(asset))
          result.decoration = "/avatar-decoration-presets/" + asset + ".png?size=160";
      }
    }
    if (const auto at = user.find("collectibles"); at != user.end()) {
      if (at->is_null()) {
        result.nameplate = "";
        result.nameplatePalette = "";
      } else if (at->is_object()) {
        if (const auto plate = at->find("nameplate"); plate != at->end()) {
          if (plate->is_null()) {
            result.nameplate = "";
            result.nameplatePalette = "";
          } else if (plate->is_object()) {
            const auto path = collectibleAsset(text(*plate, "sku_id"));
            if (!path.empty()) {
              result.nameplate = path;
              if (const auto palette = plate->find("palette"); palette != plate->end()) {
                const auto name = text(*plate, "palette");
                result.nameplatePalette = nameplateBackground(name, false) ? name : "";
              }
            }
          }
        }
      }
    }
    return result;
  }
  void mergeCosmetics(UserCosmetics& target, const UserCosmetics& patch) {
    if (patch.decoration)
      target.decoration = patch.decoration;
    if (patch.nameplate) {
      if (target.nameplate != patch.nameplate)
        target.nameplatePalette.reset();
      target.nameplate = patch.nameplate;
    }
    if (patch.nameplatePalette)
      target.nameplatePalette = patch.nameplatePalette;
  }
  EffectSample sampleProfileEffect(const ProfileEffectLayer& layer, std::chrono::milliseconds elapsed) {
    EffectSample result;
    if (layer.duration.count() <= 0)
      return result;
    auto age = elapsed - layer.start;
    if (age.count() < 0) {
      result.next = -age;
      return result;
    }
    if (layer.loop)
      age %= layer.duration + layer.loopDelay;
    if (age >= layer.duration) {
      if (layer.loop)
        result.next = layer.duration + layer.loopDelay - age;
      return result;
    }
    return {true, age, layer.duration - age};
  }
  CosmeticProduct parseCosmeticProduct(const Json& value, const std::string& sku) {
    CosmeticProduct result;
    if (!validSnowflake(sku) || !value.is_object() || text(value, "sku_id") != sku)
      return result;
    const auto items = value.find("items");
    if (items == value.end() || !items->is_array() || items->size() > maximumCollectibles)
      return result;
    for (const auto& item : *items) {
      if (!item.is_object() || !item.contains("type") || !item["type"].is_number_integer())
        continue;
      if (item["type"] == 1) {
        ProfileEffect effect;
        effect.sku = sku;
        effect.still = source(item, "reducedMotionSrc");
        if (effect.still.empty())
          effect.still = source(item, "staticFrameSrc");
        const auto layers = item.find("effects");
        if (layers != item.end() && layers->is_array() && layers->size() <= maximumLayers) {
          for (const auto& layer : *layers) {
            if (!layer.is_object())
              continue;
            auto src = source(layer, "src");
            if (src.empty())
              continue;
            ProfileEffectLayer parsed{src, time(layer, "start"), time(layer, "duration"), time(layer, "loopDelay")};
            parsed.loop = layer.contains("loop") && layer["loop"] == true;
            parsed.zIndex = static_cast<int>(number(layer, "zIndex", maximumEffectZIndex).value_or(0));
            if (const auto at = layer.find("position"); at != layer.end() && at->is_object()) {
              parsed.x = number(*at, "x", maximumFrameDimension).value_or(0);
              parsed.y = number(*at, "y", maximumFrameDimension).value_or(0);
            }
            if (parsed.duration.count() > 0)
              effect.layers.push_back(std::move(parsed));
          }
          std::ranges::stable_sort(effect.layers, {}, &ProfileEffectLayer::zIndex);
        }
        if (!effect.still.empty() || !effect.layers.empty())
          result.effect = std::move(effect);
      } else if (item["type"] == 3) {
        ProfileFrame frame;
        frame.sku = sku;
        frame.innerWidth = number(item, "inner_width", maximumFrameDimension).value_or(0);
        frame.overflowTop = number(item, "overflow_top", maximumFrameDimension).value_or(0);
        frame.overflowBottom = number(item, "overflow_bottom", maximumFrameDimension).value_or(0);
        frame.overflowHorizontal = number(item, "overflow_horizontal", maximumFrameDimension).value_or(0);
        const auto layers = item.find("layers");
        if (frame.innerWidth < 1
            || frame.overflowTop > frame.innerWidth
            || frame.overflowBottom > frame.innerWidth
            || frame.overflowHorizontal > frame.innerWidth
            || layers == item.end()
            || !layers->is_array()
            || layers->size() > maximumLayers)
          continue;
        for (const auto& layer : *layers) {
          if (!layer.is_object())
            continue;
          const auto id = text(layer, "id"), type = text(layer, "type"), anchor = text(layer, "anchor"),
                     order = text(layer, "order");
          if (!component(id)
              || (type != "staple" && type != "rail" && type != "border")
              || (anchor != "top" && anchor != "bottom" && anchor != "center")
              || (order != "front" && order != "back"))
            continue;
          frame.layers.push_back(
              {id, collectibleAsset(sku, id), type, anchor, order,
               layer.contains("responsive") && layer["responsive"] == true}
          );
        }
        if (!frame.layers.empty())
          result.frame = std::move(frame);
      }
    }
    return result;
  }
} // namespace api
