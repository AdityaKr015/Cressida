#include "app/window_icons.h"
#include "render/core/image_encoder.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {
  void require(bool ok, const char* reason) {
    if (!ok)
      throw std::runtime_error(reason);
  }
  std::span<const std::uint8_t> bytes(const std::string& text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
  }
} // namespace
int main() {
  char directory[] = "/tmp/cressida-window-icons-XXXXXX";
  if (!mkdtemp(directory))
    return 1;
  struct Cleanup {
    const char* path;
    ~Cleanup() { std::filesystem::remove_all(path); }
  } cleanup{directory};
  try {
    const std::string svg =
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="12"><path fill="currentColor" d="M0 0H24V12H0Z"/></svg>)";
    const auto red = app::WindowIcons::decode(bytes(svg), true, 24, "#ff0000");
    const auto blue = app::WindowIcons::decode(bytes(svg), true, 48, "#0000ff");
    require(
        red && blue && red->width == 24 && red->height == 12 && blue->width == 48 && blue->height == 24,
        "SVG preserves aspect ratio and rerasterizes at the requested density"
    );
    require(
        red->rgba[0] == 255 && red->rgba[2] == 0 && blue->rgba[0] == 0 && blue->rgba[2] == 255,
        "SVG currentColor follows the palette"
    );
    const std::string colored =
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"><path fill="#00ff00" d="M0 0H24V24H0Z"/></svg>)";
    const auto unchanged = app::WindowIcons::decode(bytes(colored), true, 24, "#ff0000");
    require(
        unchanged && unchanged->rgba[0] == 0 && unchanged->rgba[1] == 255, "Explicit SVG artwork colors are preserved"
    );
    const std::array<std::uint8_t, 4> pixel{21, 87, 173, 255};
    auto png = encodePng(pixel.data(), 1, 1);
    const auto raster = app::WindowIcons::decode(png, false, 24, "#ffffff");
    require(raster && std::ranges::equal(raster->rgba, pixel), "PNG icons retain original colors");
    require(!app::WindowIcons::decode(bytes("not an image"), true, 24, "#ffffff"), "Malformed SVG is rejected");
    require(!app::WindowIcons::decode(bytes(colored), false, 24, "#ffffff"), "Non-PNG raster bytes are rejected");
    png[16] = 1;
    require(
        !app::WindowIcons::decode(png, false, 24, "#ffffff"), "Oversized PNG dimensions are rejected before decoding"
    );
    const auto root = std::filesystem::path(directory);
    const auto iconPath = root / "an icon.svg";
    std::ofstream(iconPath) << svg;
    std::ofstream(root / "wrong.png") << "not an image";
    require(mkfifo((root / "pipe.svg").c_str(), 0600) == 0, "Create a FIFO fixture");
    app::WindowIconRequest request{
        {iconPath.string(), (root / "missing.svg").string(), (root / "pipe.svg").string(),
         (root / "wrong.png").string()},
        "#ff0000",
        24
    };
    const auto started = std::chrono::steady_clock::now();
    auto result = app::WindowIcons::load(request);
    require(
        result.images[0]
            && !result.images[1]
            && !result.errors[1].empty()
            && !result.images[2]
            && !result.errors[2].empty()
            && !result.errors[3].empty(),
        "Local paths with spaces work; missing, nonregular and mismatched files fail explicitly"
    );
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(1), "FIFO cannot block icon loading");
    std::ofstream(root / "large.svg") << std::string(app::WindowIcons::maximumBytes + 1, 'x');
    request.paths = {(root / "large.svg").string(), "", "", ""};
    require(!app::WindowIcons::load(request).errors[0].empty(), "Oversized files are bounded before decoding");
    require(
        !app::WindowIcons::validPath("relative.svg")
            && !app::WindowIcons::validPath("https://example.com/icon.svg")
            && !app::WindowIcons::validPath(std::string("/icon\0.svg", 10))
            && app::WindowIcons::validPath(""),
        "Only absolute local paths or empty fallback values are accepted"
    );
    app::WindowIcons worker;
    request.paths = {iconPath.string(), "", "", ""};
    worker.request(request);
    request.color = "#0000ff";
    const auto generation = worker.request(request);
    std::optional<app::WindowIconResult> delivered;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
      pollfd descriptor{worker.eventFd(), POLLIN, 0};
      require(poll(&descriptor, 1, 1000) >= 0, "Worker exposes a native event-loop wake descriptor");
      if (auto value = worker.poll(); value && value->generation == generation) {
        delivered = std::move(value);
        break;
      }
    }
    require(
        delivered && delivered->images[0] && delivered->images[0]->rgba[2] == 255,
        "Latest immutable worker result carries its generation and current palette"
    );
    std::cout << "Custom window icon I/O, decoding and worker checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
