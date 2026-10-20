#pragma once

#include "app/window_controls.h"
#include "core/process/wake_event.h"
#include "render/core/image_file_loader.h"

#include <condition_variable>
#include <expected>
#include <mutex>
#include <optional>
#include <span>
#include <thread>

namespace app {
  struct WindowIconRequest {
    std::array<std::string, windowIconNames.size()> paths;
    std::string color;
    int pixels = 0;
    bool operator==(const WindowIconRequest&) const = default;
  };
  struct WindowIconResult {
    std::uint64_t generation = 0;
    std::array<std::optional<LoadedImageFile>, windowIconNames.size()> images;
    std::array<std::string, windowIconNames.size()> errors;
  };
  // Local icon I/O and CPU decoding run on a coalescing worker. Only the owning
  // application loop uploads its immutable result to the existing renderer.
  class WindowIcons {
  public:
    static constexpr std::size_t maximumBytes = 256 * 1024, maximumPathBytes = maximumWindowIconPathBytes;
    static constexpr std::uint32_t maximumPngDimension = 1024;
    static constexpr int maximumRasterSize = 256;
    WindowIcons();
    ~WindowIcons();
    int eventFd() const { return m_wake.fd(); }
    std::uint64_t request(WindowIconRequest request);
    std::optional<WindowIconResult> poll();
    static bool validPath(const std::string& path);
    static std::expected<LoadedImageFile, std::string>
    decode(std::span<const std::uint8_t> bytes, bool svg, int pixels, const std::string& color);
    static WindowIconResult load(const WindowIconRequest& request, std::stop_token stop = {});

  private:
    void run(std::stop_token stop);
    process::WakeEvent m_wake;
    std::mutex m_mutex;
    std::condition_variable_any m_ready;
    std::optional<std::pair<std::uint64_t, WindowIconRequest>> m_pending;
    std::optional<WindowIconResult> m_result;
    std::uint64_t m_generation = 0;
    std::jthread m_worker;
  };
} // namespace app
