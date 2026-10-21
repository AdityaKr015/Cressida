#include "app/image_cache.h"
#include "render/core/texture_manager.h"

#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace {
  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }

  class FakeTextureManager final : public TextureManager {
  public:
    std::map<std::uint32_t, std::size_t> allocations;
    std::size_t unloads = 0;

    TextureHandle make(std::size_t bytes) {
      TextureHandle handle;
      handle.id = TextureId(++serial);
      handle.generation = 1;
      handle.allocationBytes = bytes;
      allocations.emplace(handle.id.value(), bytes);
      return handle;
    }
    TextureHandle loadFromFile(const std::string&, int, bool) override { return {}; }
    TextureHandle loadFromEncodedBytes(const std::uint8_t*, std::size_t, bool) override { return {}; }
    TextureHandle loadFromRgba(const std::uint8_t*, int, int, bool) override { return {}; }
    TextureHandle loadFromRaw(const std::uint8_t*, std::size_t, int, int, int, PixmapFormat, bool) override {
      return {};
    }
    TextureHandle loadFromPixels(const std::uint8_t*, int, int, TextureDataFormat, TextureFilter, bool) override {
      return {};
    }
    TextureHandle createEmpty(int, int, TextureDataFormat, TextureFilter) override { return {}; }
    bool replace(TextureHandle&, const std::uint8_t*, int, int, TextureDataFormat, TextureFilter, bool) override {
      return false;
    }
    bool updateSubImage(TextureHandle&, const std::uint8_t*, int, int, int, int, TextureDataFormat) override {
      return false;
    }
    void unload(TextureHandle& handle) override {
      require(handle.valid() && allocations.erase(handle.id.value()) == 1, "Unload a live texture exactly once");
      ++unloads;
      handle = {};
    }
    void cleanup() override { allocations.clear(); }
    void abandonGpuResources() noexcept override { allocations.clear(); }
    void probeExtensions() override {}

  private:
    std::uint32_t serial = 0;
  };

  app::ImageCacheLimits unlimited() {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    return {maximum, maximum, maximum, maximum};
  }

  std::shared_ptr<const media::Animation> animation(std::size_t bytes) {
    auto result = std::make_shared<media::Animation>();
    result->frames.resize(2);
    result->frames.front().rgba.resize(bytes / 2);
    result->frames.back().rgba.resize(bytes - bytes / 2);
    return result;
  }

  void testEncodedStorage() {
    app::ImageCache cache;
    auto& old = cache["old"];
    old.bytes.reserve(512);
    old.bytes.resize(256);
    old.bytes.clear();
    old.touched = 1;
    old.visibleDraw = 9;
    auto& recent = cache["recent"];
    recent.bytes.reserve(1024);
    recent.bytes.resize(64);
    recent.touched = 2;
    const auto recentCapacity = recent.bytes.capacity();
    auto limits = unlimited();
    limits.encodedBytes = recentCapacity;
    require(
        app::imageCacheUsage(cache).encodedBytes == old.bytes.capacity() + recentCapacity,
        "Encoded usage includes spare capacity even when an earlier clear left the vector empty"
    );
    require(app::trimImageCache(cache, nullptr, 9, limits), "Trim encoded allocation above its own budget");
    require(
        cache.size() == 2 && old.bytes.capacity() == 0 && recent.bytes.capacity() == recentCapacity,
        "Release the oldest encoded allocation completely while preserving its visible decoded entry"
    );
    require(!app::trimImageCache(cache, nullptr, 9, limits), "Repeated trimming below budget makes no changes");
    limits.encodedBytes = 0;
    require(
        !app::trimImageCache(cache, nullptr, 9, limits, "recent") && recent.bytes.capacity() == recentCapacity,
        "An original needed for an image action retains encoded bytes above budget"
    );
    require(
        app::trimImageCache(cache, nullptr, 9, limits) && app::imageCacheUsage(cache).encodedBytes == 0,
        "Finishing the image action allows encoded storage to be released"
    );
  }

  void testAnimationEviction() {
    FakeTextureManager textures;
    app::ImageCache cache;
    cache["static"].touched = 0;
    cache["old"].touched = 1;
    cache["old"].animation = animation(8);
    cache["old"].texture = textures.make(4);
    cache["recent"].touched = 2;
    cache["recent"].animation = animation(8);
    auto limits = unlimited();
    limits.animationBytes = 8;
    require(app::trimImageCache(cache, &textures, 0, limits), "Trim the decoded animation budget independently");
    require(
        !cache.contains("old") && cache.contains("recent") && cache.contains("static") && textures.unloads == 1,
        "Evict the oldest animation and its texture without removing unrelated static entries"
    );
    require(app::imageCacheUsage(cache).animationBytes == 8, "Decoded animation residency meets its own budget");
    const auto [revisited, inserted] = cache.try_emplace("old");
    require(
        inserted && !revisited->second.texture.valid() && !revisited->second.animation,
        "A revisited evicted animation is a complete cache miss so it can load and animate again"
    );
  }

  void testTextureEviction() {
    FakeTextureManager textures;
    app::ImageCache cache;
    cache["empty"].touched = 0;
    cache["old"].touched = 1;
    cache["old"].texture = textures.make(128);
    cache["recent"].touched = 2;
    cache["recent"].texture = textures.make(256);
    const auto recentId = cache["recent"].texture.id.value();
    auto limits = unlimited();
    limits.textureBytes = 256;
    require(app::trimImageCache(cache, &textures, 0, limits), "Trim GPU allocation above its own byte budget");
    require(
        !cache.contains("old")
            && cache.contains("recent")
            && cache.contains("empty")
            && textures.unloads == 1
            && textures.allocations.contains(recentId)
            && app::imageCacheUsage(cache).textureBytes == 256,
        "Texture byte pressure frees old native storage without removing entries that own no textures"
    );
    limits.textureBytes = 0;
    require(
        app::trimImageCache(cache, &textures, 0, limits) && textures.allocations.empty(),
        "All evicted native texture allocations are released"
    );
  }

  void testEntryEviction() {
    app::ImageCache cache;
    cache["lexically-first"].touched = 9;
    cache["lexically-last"].touched = 1;
    auto limits = unlimited();
    limits.entries = 1;
    require(
        app::trimImageCache(cache, nullptr, 0, limits) && cache.size() == 1 && cache.contains("lexically-first"),
        "The entry limit uses access age rather than path order"
    );
  }

  void testWorkingSet() {
    FakeTextureManager textures;
    app::ImageCache cache;
    for (const auto* path : {"hidden", "protected", "visible"}) {
      auto& image = cache[path];
      image.texture = textures.make(64);
      image.animation = animation(8);
      image.bytes.resize(16);
    }
    cache["visible"].visibleDraw = 7;
    app::ImageCacheLimits limits{0, 0, 0, 0};
    require(app::trimImageCache(cache, &textures, 7, limits, "protected"), "Trim hidden entries under pressure");
    require(
        !cache.contains("hidden")
            && cache.contains("protected")
            && cache.contains("visible")
            && cache["visible"].bytes.capacity() == 0
            && cache["protected"].bytes.capacity() > 0
            && textures.unloads == 1,
        "Current visible art and a pending original remain usable even if their working set exceeds all budgets"
    );
    require(
        !app::trimImageCache(cache, &textures, 7, limits, "protected"),
        "A protected working set is stable across repeated trims instead of thrashing"
    );
    require(
        app::trimImageCache(cache, &textures, 8, limits) && cache.empty() && textures.allocations.empty(),
        "Leaving the working set makes all resources eligible for eviction"
    );
  }

  void testLocalPreviews() {
    FakeTextureManager textures;
    app::ImageCache cache;
    cache["local"].texture = textures.make(128);
    cache["local"].reloadable = false;
    cache["remote"].texture = textures.make(128);
    app::ImageCacheLimits limits{0, 0, 0, 0};
    require(
        app::trimImageCache(cache, &textures, 0, limits)
            && cache.size() == 1
            && cache.contains("local")
            && textures.unloads == 1,
        "Local previews survive budget pressure because their source cannot be fetched again"
    );
    require(
        !app::trimImageCache(cache, &textures, 0, limits) && cache["local"].texture.valid(),
        "Repeated pressure never discards an irreplaceable preview"
    );
    textures.unload(cache["local"].texture);
  }

  void testMissingContext() {
    FakeTextureManager textures;
    app::ImageCache cache;
    cache["gpu"].texture = textures.make(128);
    cache["gpu"].bytes.resize(32);
    cache["cpu"].animation = animation(8);
    app::ImageCacheLimits limits{0, 0, 0, 0};
    require(
        app::trimImageCache(cache, nullptr, 0, limits)
            && cache.contains("gpu")
            && !cache.contains("cpu")
            && cache["gpu"].bytes.capacity() == 0
            && textures.unloads == 0
            && textures.allocations.size() == 1,
        "Without a current graphics context CPU storage can be freed but native texture ownership is retained"
    );
    require(
        app::trimImageCache(cache, &textures, 0, limits) && cache.empty() && textures.allocations.empty(),
        "Restoring the graphics context releases the retained native texture instead of orphaning it"
    );
  }

  void testImageOwners() {
    const auto content = app::imageUseMask(app::ImageUse::Content);
    const auto effect = app::imageUseMask(app::ImageUse::ProfileEffect);
    const auto frame = app::imageUseMask(app::ImageUse::ProfileFrame);
    FakeTextureManager textures;
    app::ImageCache cache;
    cache["effect"].uses = effect;
    cache["effect"].texture = textures.make(128);
    cache["effect"].bytes.resize(32);
    cache["effect"].animation = animation(16);
    std::weak_ptr<const media::Animation> decoded = cache["effect"].animation;
    cache["shared-content"].uses = content | effect;
    cache["shared-content"].texture = textures.make(64);
    cache["shared-frame"].uses = frame | effect;
    cache["shared-frame"].texture = textures.make(64);
    cache["unrelated"].uses = content;
    cache["unclassified"].texture = textures.make(64);
    auto unused = app::releaseImageUses(cache, effect);
    require(
        unused == std::vector<std::string>{"effect"}
            && cache["shared-content"].uses == content
            && cache["shared-frame"].uses == frame
            && cache["unrelated"].uses == content,
        "Disable releases only the category's final owners and preserves other media and enabled categories"
    );
    for (const auto& key : unused) {
      textures.unload(cache.at(key).texture);
      cache.erase(key);
    }
    require(decoded.expired(), "Discarding the final owner releases its decoded animation storage");
    require(
        app::imageCacheUsage(cache).encodedBytes == 0 && textures.unloads == 1,
        "Discarding disabled artwork releases both encoded storage and its native texture"
    );
    require(
        app::releaseImageUses(cache, effect).empty(),
        "Repeated disable is idempotent and never discards unclassified or unrelated assets"
    );
    unused = app::releaseImageUses(cache, frame);
    require(
        unused == std::vector<std::string>{"shared-frame"}, "The last enabled cosmetic category releases shared artwork"
    );
    cache["effect"].uses |= effect;
    require(
        !cache["effect"].texture.valid() && !cache["effect"].animation && cache["effect"].bytes.empty(),
        "Reenable starts with a clean asset that can be loaded on demand"
    );
    for (const auto& [_, image] : cache) {
      auto texture = image.texture;
      if (texture.valid())
        textures.unload(texture);
    }

    for (const auto category :
         {app::ImageUse::AvatarDecoration, app::ImageUse::Nameplate, app::ImageUse::ProfileEffect,
          app::ImageUse::ProfileFrame, app::ImageUse::ProfileBanner}) {
      app::ImageCache individual;
      individual["static"].uses = app::imageUseMask(category);
      individual["animated"].uses = app::imageUseMask(category);
      individual["shared"].uses = content | app::imageUseMask(category);
      require(
          app::releaseImageUses(individual, app::imageUseMask(category))
                  == std::vector<std::string>{"animated", "static"}
              && individual["shared"].uses == content,
          "Every cosmetic category releases static and animated variants while preserving shared content"
      );
    }
  }

  void testTextureReplacement() {
    FakeTextureManager textures;
    app::CachedImage cached;
    cached.texture = textures.make(128);
    const auto oldId = cached.texture.id;
    require(
        !app::replaceCachedImageTexture(cached, textures, {})
            && cached.texture.id == oldId
            && textures.allocations.contains(oldId.value())
            && textures.unloads == 0,
        "A failed replacement preserves the previous usable image and its native allocation"
    );
    require(
        app::replaceCachedImageTexture(cached, textures, cached.texture) && textures.unloads == 0,
        "Reusing the same handle and generation never deletes the active native image"
    );
    const auto next = textures.make(256);
    require(
        app::replaceCachedImageTexture(cached, textures, next)
            && cached.texture.id == next.id
            && cached.texture.allocationBytes == 256
            && !textures.allocations.contains(oldId.value())
            && textures.allocations.contains(next.id.value())
            && textures.unloads == 1,
        "A successful replacement deletes the superseded object and retains only the newly uploaded image"
    );
    textures.unload(cached.texture);
    require(textures.allocations.empty(), "All textures created during replacement checks are released");
  }
} // namespace

int main() {
  try {
    testEncodedStorage();
    testAnimationEviction();
    testTextureEviction();
    testEntryEviction();
    testWorkingSet();
    testLocalPreviews();
    testMissingContext();
    testTextureReplacement();
    testImageOwners();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
