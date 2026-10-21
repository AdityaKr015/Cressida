#pragma once

#include "media/animation.h"
#include "render/core/texture_handle.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class TextureManager;

namespace app {
  inline constexpr std::size_t imageAnimationBudget = 64 * 1024 * 1024;
  inline constexpr std::size_t imageEncodedBudget = 16 * 1024 * 1024;
  inline constexpr std::size_t imageTextureBudget = 64 * 1024 * 1024;
  inline constexpr std::size_t imageEntryBudget = 384;

  enum class ImageUse : std::uint8_t {
    Content = 1 << 0,
    AvatarDecoration = 1 << 1,
    Nameplate = 1 << 2,
    ProfileEffect = 1 << 3,
    ProfileFrame = 1 << 4,
    ProfileBanner = 1 << 5
  };
  using ImageUses = std::uint8_t;

  constexpr ImageUses imageUseMask(ImageUse use) { return static_cast<ImageUses>(use); }

  struct CachedImage {
    TextureHandle texture;
    std::vector<unsigned char> bytes;
    std::uint64_t touched = 0;
    std::chrono::steady_clock::time_point retry{};
    std::shared_ptr<const media::Animation> animation;
    std::chrono::steady_clock::time_point animationStart{};
    std::size_t animationFrame = 0;
    std::uint64_t visibleFrame = 0, playingDraw = 0;
    std::uint64_t visibleDraw = 0;
    bool reloadable = true;
    ImageUses uses = 0;
  };

  using ImageCache = std::map<std::string, CachedImage>;

  struct ImageCacheLimits {
    std::size_t animationBytes = imageAnimationBudget;
    std::size_t encodedBytes = imageEncodedBudget;
    std::size_t textureBytes = imageTextureBudget;
    std::size_t entries = imageEntryBudget;
  };

  struct ImageCacheUsage {
    std::size_t animationBytes = 0;
    std::size_t encodedBytes = 0;
    std::size_t textureBytes = 0;
    std::size_t entries = 0;
  };

  ImageCacheUsage imageCacheUsage(const ImageCache& cache);

  // Return assets whose final owner was removed, for the caller to cancel and discard.
  std::vector<std::string> releaseImageUses(ImageCache& cache, ImageUses removed);

  // A failed upload leaves the old usable texture in place.
  bool replaceCachedImageTexture(CachedImage& cached, TextureManager& textures, TextureHandle next);

  // Visible assets and an original awaiting an image action form the working
  // set. It may exceed the limits rather than reload on every draw.
  bool trimImageCache(
      ImageCache& cache, TextureManager* textures, std::uint64_t visibleDraw, const ImageCacheLimits& limits = {},
      std::string_view protectedPath = {}
  );
} // namespace app
