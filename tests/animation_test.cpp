#include "media/animation.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
  void require(bool value, const char* text) {
    if (!value)
      throw std::runtime_error(text);
  }
  std::vector<std::uint8_t> read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  }
} // namespace
int main(int argc, char** argv) {
  try {
    require(argc == 2, "Animation fixture directory required");
    for (const auto* name : {"clip.gif", "clip.webp", "clip.mp4", "clip.webm"}) {
      const auto bytes = read(std::filesystem::path(argv[1]) / name);
      const auto animation = media::decodeAnimation(bytes);
      if (!animation)
        throw std::runtime_error(std::string("Decode failed: ") + name);
      if (animation->width != 32 || animation->height != 24 || animation->frames.size() != 6)
        std::cerr
            << name
            << ": "
            << animation->width
            << 'x'
            << animation->height
            << ", "
            << animation->frames.size()
            << " frames\n";
      require(
          animation->width == 32 && animation->height == 24 && animation->frames.size() == 6,
          "Decode all GIF/WebP/MP4/WebM frames at canvas size"
      );
      require(
          animation->duration.count() >= 500 && animation->duration.count() <= 700,
          "Preserve frame timing, including delayed video frames"
      );
      require(
          animation->frames.front().rgba != animation->frames.back().rgba,
          "Decoded animation must actually change pixels"
      );
      require(
          animation->frameAt(std::chrono::milliseconds(0)) == 0
              && animation->frameAt(animation->frames.front().duration) == 1
              && animation->frameAt(animation->duration) == 0
              && animation->frameAt(animation->duration * 2 + animation->frames.front().duration) == 1,
          "Animation advances at duration boundaries and loops without restarting on repaint"
      );
      require(animation->byteSize() == 32 * 24 * 4 * 6, "Account for every decoded frame in the memory budget");
      std::stop_source cancelled;
      cancelled.request_stop();
      require(!media::decodeAnimation(bytes, cancelled.get_token()), "Cancelled jobs do not publish media");
    }
    for (const auto& [width, height] :
         {std::pair{10, 2}, {26, 2}, {18, 30}, {34, 50}, {17, 23}, {31, 47}, {174, 512}}) {
      for (const auto* extension : {"gif", "webp", "mp4", "webm"}) {
        const auto bytes = read(
            std::filesystem::path(argv[1])
            / ("unaligned-" + std::to_string(width) + "x" + std::to_string(height) + "." + extension)
        );
        for (int repeat = 0; repeat < 12; ++repeat) {
          const auto animation = media::decodeAnimation(bytes);
          const auto ratio =
              std::min(1.0, (std::string_view(extension) == "webp" ? 512.0 : 384.0) / std::max(width, height));
          require(
              animation
                  && animation->width == int(width * ratio)
                  && animation->height == int(height * ratio)
                  && animation->frames.size() == 6,
              "Decode non-aligned and scaled portrait media repeatedly without corrupting memory"
          );
          const auto frameBytes = std::size_t(animation->width) * animation->height * 4;
          require(animation->byteSize() == frameBytes * 6, "Export packed pixels without conversion padding");
          for (const auto& frame : animation->frames) {
            require(frame.rgba.size() == frameBytes, "Every portrait frame has exactly one packed canvas");
            require(frame.rgba[3] == 255 && frame.rgba.back() == 255, "Preserve alpha through the last pixel");
            const auto last = frameBytes - 4;
            for (int component = 0; component < 3; ++component)
              require(
                  std::abs(int(frame.rgba[component]) - int(frame.rgba[last + component])) <= 8,
                  "Converted portrait rows preserve their solid color through the last pixel"
              );
          }
        }
      }
    }
    const auto capped = media::decodeAnimation(read(std::filesystem::path(argv[1]) / "long.gif"));
    const auto resized = media::decodeAnimation(read(std::filesystem::path(argv[1]) / "large.gif"));
    require(
        resized && resized->frames.size() == 300 && resized->width <= 256 && resized->byteSize() <= 64 * 1024 * 1024,
        "Downscale long GIFs to retain their complete loop inside the memory budget"
    );
    require(
        capped && capped->frames.size() == 1, "An over-limit animation uses its still frame instead of a partial loop"
    );
    for (auto bytes :
         {std::vector<std::uint8_t>{}, std::vector<std::uint8_t>(128, 0),
          std::vector<std::uint8_t>{'G', 'I', 'F', '8', '9', 'a', 0, 0, 0, 0, 0, 0}})
      require(!media::decodeAnimation(bytes), "Reject malformed/unsupported media safely");
    media::Animation empty;
    require(
        empty.frameAt(std::chrono::milliseconds(-10)) == 0 && empty.byteSize() == 0,
        "Empty and negative playback times are safe"
    );
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
