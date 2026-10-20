#include "app/window_icons.h"

#include "app/file_picker.h"

#include <glib.h>
#include <utility>

namespace app {
  WindowIcons::WindowIcons() : m_worker([this](std::stop_token stop) { run(stop); }) {}
  WindowIcons::~WindowIcons() {
    m_worker.request_stop();
    m_ready.notify_all();
    m_worker.join();
  }
  std::uint64_t WindowIcons::request(WindowIconRequest request) {
    std::lock_guard lock(m_mutex);
    m_pending = {{++m_generation, std::move(request)}};
    m_ready.notify_all();
    return m_generation;
  }
  std::optional<WindowIconResult> WindowIcons::poll() {
    m_wake.drain();
    std::lock_guard lock(m_mutex);
    return std::exchange(m_result, {});
  }
  bool WindowIcons::validPath(const std::string& path) {
    return path.size() <= maximumPathBytes
        && path.find('\0') == std::string::npos
        && (path.empty() || path.front() == '/');
  }
  std::expected<LoadedImageFile, std::string>
  WindowIcons::decode(std::span<const std::uint8_t> bytes, bool svg, int pixels, const std::string& color) {
    if (bytes.empty() || bytes.size() > maximumBytes)
      return std::unexpected("Choose an SVG or PNG no larger than 256 KiB.");
    std::string source(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (svg) {
      // SVG currentColor is the opt-in palette color. Explicit fills/strokes,
      // including colored SVG artwork, are kept intact.
      for (std::size_t at = 0; at + std::string_view("currentColor").size() <= source.size();) {
        const auto length = std::string_view("currentColor").size();
        if (g_ascii_strncasecmp(source.data() + at, "currentColor", length) == 0) {
          source.replace(at, length, color);
          at += color.size();
        } else
          ++at;
      }
    } else {
      constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
      constexpr std::size_t pngDimensionHeaderSize = 24, pngWidthOffset = 16, pngHeightOffset = 20;
      if (bytes.size() < pngDimensionHeaderSize || !std::ranges::equal(bytes.first(signature.size()), signature))
        return std::unexpected("Choose a valid SVG or PNG image.");
      const auto dimension = [&](std::size_t offset) {
        return (std::uint32_t(bytes[offset]) << 24)
            | (std::uint32_t(bytes[offset + 1]) << 16)
            | (std::uint32_t(bytes[offset + 2]) << 8)
            | bytes[offset + 3];
      };
      if (dimension(pngWidthOffset) > maximumPngDimension || dimension(pngHeightOffset) > maximumPngDimension)
        return std::unexpected("PNG icons must be at most 1024 pixels on each side.");
    }
    auto* encoded = g_base64_encode(reinterpret_cast<const guchar*>(source.data()), source.size());
    const auto uri = std::string(svg ? "data:image/svg+xml;base64," : "data:image/png;base64,") + encoded;
    g_free(encoded);
    auto result = loadImageFile(uri, std::clamp(pixels, 1, maximumRasterSize));
    if (!result)
      return std::unexpected("Could not decode this icon. Choose a valid SVG or PNG.");
    return result;
  }
  WindowIconResult WindowIcons::load(const WindowIconRequest& request, std::stop_token stop) {
    WindowIconResult result;
    const std::array<std::string, 2> types{"image/svg+xml", "image/png"};
    for (std::size_t i = 0; i < request.paths.size() && !stop.stop_requested(); ++i) {
      const auto& path = request.paths[i];
      if (path.empty())
        continue;
      if (!validPath(path)) {
        result.errors[i] = "Enter an absolute local file path.";
        continue;
      }
      auto* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
      auto file = FilePicker::readSelection(uri ? uri : "", types, maximumBytes);
      g_free(uri);
      if (!file.error.empty()) {
        result.errors[i] = "Could not read this icon. Use a local SVG or PNG up to 256 KiB.";
        continue;
      }
      auto image = decode(file.bytes, file.mime == "image/svg+xml", request.pixels, request.color);
      if (image)
        result.images[i] = std::move(*image);
      else
        result.errors[i] = image.error();
    }
    return result;
  }
  void WindowIcons::run(std::stop_token stop) {
    while (!stop.stop_requested()) {
      std::unique_lock lock(m_mutex);
      if (!m_ready.wait(lock, stop, [this] { return m_pending.has_value(); }))
        return;
      auto [generation, request] = std::move(*m_pending);
      m_pending.reset();
      lock.unlock();
      auto result = load(request, stop);
      result.generation = generation;
      lock.lock();
      if (!stop.stop_requested() && generation == m_generation) {
        m_result = std::move(result);
        (void)m_wake.signal();
      }
    }
  }
} // namespace app
