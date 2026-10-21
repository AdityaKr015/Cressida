#pragma once
#include <chrono>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace api {
  // Missing fields preserve previous snapshots; empty values explicitly clear them.
  struct UserCosmetics {
    std::optional<std::string> decoration, nameplate, nameplatePalette;
    bool operator==(const UserCosmetics&) const = default;
  };
  std::optional<std::uint32_t> nameplateBackground(std::string_view palette, bool light);
  UserCosmetics parseUserCosmetics(const nlohmann::json& user);
  void mergeCosmetics(UserCosmetics& target, const UserCosmetics& patch);
  struct ProfileEffectLayer {
    std::string source;
    std::chrono::milliseconds start{}, duration{}, loopDelay{};
    bool loop = false;
    int zIndex = 0;
    float x = 0, y = 0;
  };
  struct EffectSample {
    bool visible = false;
    std::chrono::milliseconds elapsed{}, next = std::chrono::milliseconds::max();
  };
  EffectSample sampleProfileEffect(const ProfileEffectLayer& layer, std::chrono::milliseconds elapsed);
  struct ProfileEffect {
    std::string sku, still;
    std::vector<ProfileEffectLayer> layers;
  };
  struct ProfileFrameLayer {
    std::string id, source, type, anchor, order;
    bool responsive = false;
  };
  struct ProfileFrame {
    std::string sku;
    float innerWidth = 0, overflowTop = 0, overflowBottom = 0, overflowHorizontal = 0;
    std::vector<ProfileFrameLayer> layers;
  };
  struct CosmeticProduct {
    std::optional<ProfileEffect> effect;
    std::optional<ProfileFrame> frame;
  };
  CosmeticProduct parseCosmeticProduct(const nlohmann::json& value, const std::string& sku);
  std::string cosmeticAssetVariant(const std::string& path, bool animated);
  std::string collectibleAsset(const std::string& sku, std::string_view id = {}, bool animated = false);
} // namespace api
