#include "app/file_picker.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gio/gio.h>
#include <iostream>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
  using namespace std::chrono_literals;
  void require(bool value, const char* description) {
    if (!value)
      throw std::runtime_error(description);
  }
  class TemporaryDirectory {
  public:
    TemporaryDirectory() {
      std::array<char, 64> pattern{};
      std::strcpy(pattern.data(), "/tmp/cressida-file-picker-test-XXXXXX");
      const auto* path = mkdtemp(pattern.data());
      if (!path)
        throw std::runtime_error("Could not create the file picker test directory");
      m_path = path;
    }
    ~TemporaryDirectory() {
      std::error_code ignored;
      std::filesystem::remove_all(m_path, ignored);
    }
    std::filesystem::path file(const std::string& name) const { return m_path / name; }

  private:
    std::filesystem::path m_path;
  };
  class PrivateBusAddress {
  public:
    explicit PrivateBusAddress(const std::filesystem::path& socket) {
      if (const auto* previous = std::getenv("DBUS_SESSION_BUS_ADDRESS"))
        m_previous = previous;
      const auto address = "unix:path=" + socket.string();
      require(setenv("DBUS_SESSION_BUS_ADDRESS", address.c_str(), 1) == 0, "Set an isolated file picker bus address");
    }
    ~PrivateBusAddress() {
      if (m_previous)
        (void)setenv("DBUS_SESSION_BUS_ADDRESS", m_previous->c_str(), 1);
      else
        (void)unsetenv("DBUS_SESSION_BUS_ADDRESS");
    }

  private:
    std::optional<std::string> m_previous;
  };
  // Accept a private Unix connection without speaking D-Bus. This holds the
  // worker in its cancellable connection operation and never opens a real chooser.
  class UnresponsiveBus {
  public:
    explicit UnresponsiveBus(const std::filesystem::path& path) {
      const auto name = path.string();
      sockaddr_un address{};
      address.sun_family = AF_UNIX;
      require(name.size() < sizeof(address.sun_path), "Private test bus path fits a Unix socket address");
      std::memcpy(address.sun_path, name.c_str(), name.size() + 1);
      m_socket = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
      require(m_socket >= 0, "Create an isolated fake bus socket");
      require(
          bind(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "Bind the isolated fake bus"
      );
      require(listen(m_socket, 4) == 0, "Listen for the file picker worker");
    }
    ~UnresponsiveBus() {
      if (m_peer >= 0)
        close(m_peer);
      if (m_socket >= 0)
        close(m_socket);
    }
    void connected() {
      pollfd incoming{m_socket, POLLIN, 0};
      int status;
      do {
        status = ::poll(&incoming, 1, 5000);
      } while (status < 0 && errno == EINTR);
      require(status > 0 && (incoming.revents & POLLIN), "The picker worker reaches the isolated fake bus");
      if (m_peer >= 0)
        close(m_peer);
      m_peer = accept4(m_socket, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
      require(m_peer >= 0, "Accept the worker without contacting a desktop portal");
    }

  private:
    int m_socket = -1, m_peer = -1;
  };
  std::string uri(const std::filesystem::path& path) {
    auto* encoded = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    require(encoded, "Encode a temporary local file URI");
    std::string result(encoded);
    g_free(encoded);
    return result;
  }
  void write(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(bool(file), "Write a temporary upload fixture");
  }
  app::PickedFile waitForResult(app::FilePicker& picker) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    do {
      if (auto result = picker.poll())
        return std::move(*result);
      const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
      if (remaining <= 0ms)
        break;
      pollfd wake{picker.eventFd(), POLLIN, 0};
      const auto status = ::poll(&wake, 1, static_cast<int>(remaining.count()));
      if (status < 0 && errno == EINTR)
        continue;
      require(status > 0 && (wake.revents & POLLIN), "The file picker signals a result within five seconds");
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("The file picker did not publish its result");
  }
} // namespace

int main() {
  try {
    TemporaryDirectory directory;
    const std::vector<std::string> images{"image/png", "image/jpeg", "image/gif", "image/webp"};
    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I',  'H',  'D',  'R', 0,
                                  0,    0,   1,   0,   0,    0,    1,    8,    6, 0, 0, 0,  0x1F, 0x15, 0xC4, 0x89};
    png.resize(128);
    const auto selected = directory.file("an image #1.txt");
    write(selected, png);
    auto result = app::FilePicker::readSelection(uri(selected), images, png.size());
    require(
        result.error.empty()
            && !result.cancelled
            && result.mime == "image/png"
            && result.bytes == png
            && result.name == selected.filename().string(),
        "Use sniffed bytes instead of the extension, preserving exact bounded bytes and a decoded local filename"
    );
    const auto localhostUri = "file://localhost" + uri(selected).substr(7);
    require(
        app::FilePicker::readSelection(localhostUri, images, png.size()).bytes == png,
        "Accept a local file URI with the explicit localhost authority"
    );
    result = app::FilePicker::readSelection(uri(selected), images, png.size() - 1);
    require(
        !result.error.empty() && result.bytes.empty(),
        "Reject one byte above the requested limit without retaining content"
    );
    result = app::FilePicker::readSelection(uri(selected), images, 0);
    require(!result.error.empty() && result.bytes.empty(), "A zero-byte upload allowance rejects nonempty files");
    result = app::FilePicker::readSelection(uri(selected), std::vector<std::string>{"image/jpeg"}, 512);
    require(
        !result.error.empty() && result.bytes.empty(), "Sniffed MIME must also belong to the requested upload types"
    );
    result = app::FilePicker::readSelection(uri(selected), {}, 512);
    require(
        !result.error.empty() && result.bytes.empty(), "An empty MIME allowlist never accepts arbitrary local bytes"
    );

    const auto mislabeled = directory.file("plain-text.png");
    const std::string text = "This is plain text rather than an uploaded image.\n";
    write(mislabeled, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    result = app::FilePicker::readSelection(uri(mislabeled), images, 512);
    require(
        !result.error.empty() && result.bytes.empty(), "An image extension cannot turn text into an accepted image"
    );
    const auto jpeg = directory.file("a-jpeg.png");
    const std::array<std::uint8_t, 20> jpegHeader{0xFF, 0xD8, 0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F',
                                                  0,    1,    1,    0,    0, 1,  0,   1,   0,   0};
    write(jpeg, jpegHeader);
    result = app::FilePicker::readSelection(uri(jpeg), std::vector<std::string>{"image/png"}, 512);
    require(
        !result.error.empty() && result.bytes.empty(), "A substituted JPEG header is rejected by a PNG-only upload"
    );
    result = app::FilePicker::readSelection(uri(jpeg), images, 512);
    require(result.error.empty() && result.mime == "image/jpeg", "Recognize JPEG content despite a PNG filename");

    const auto empty = directory.file("empty.png");
    write(empty, {});
    result = app::FilePicker::readSelection(uri(empty), images, 512);
    require(!result.error.empty() && result.bytes.empty(), "Reject an empty selected file");
    const auto oversized = directory.file("oversized.png");
    write(oversized, png);
    std::filesystem::resize_file(oversized, 10 * 1024 * 1024 + 1);
    result = app::FilePicker::readSelection(uri(oversized), images, 20 * 1024 * 1024);
    require(
        !result.error.empty() && result.bytes.empty(),
        "The hard ten-MiB read ceiling overrides larger caller allowances"
    );
    result = app::FilePicker::readSelection(uri(directory.file("missing.png")), images, 512);
    require(
        !result.error.empty() && result.bytes.empty(), "Return a normal error when the selected file has disappeared"
    );
    result = app::FilePicker::readSelection(uri(directory.file(".")), images, 512);
    require(!result.error.empty() && result.bytes.empty(), "Directories cannot be uploaded as files");
    const auto fifo = directory.file("named-pipe.png");
    require(mkfifo(fifo.c_str(), 0600) == 0, "Create a non-regular local file fixture");
    const auto readStart = std::chrono::steady_clock::now();
    result = app::FilePicker::readSelection(uri(fifo), images, 512);
    require(
        !result.error.empty() && result.bytes.empty() && std::chrono::steady_clock::now() - readStart < 1s,
        "A named pipe cannot block bounded local file reads or application shutdown"
    );

    const std::vector<std::string> rejectedUris{
        "https://example.invalid/image.png",
        "data:image/png;base64,iVBORw0K",
        "file://remote.invalid/tmp/image.png",
        "file://localhost.invalid/tmp/image.png",
        "file://127.0.0.1/tmp/image.png",
        "file://relative-path",
        "file:///tmp/image%00.png",
        std::string("file:///tmp/image\0.png", 22),
        "file:///" + std::string(16384, 'x')
    };
    for (const auto& rejected : rejectedUris) {
      result = app::FilePicker::readSelection(rejected, images, 512);
      require(
          !result.error.empty() && result.bytes.empty(),
          "Reject remote, malformed, embedded-NUL and overlong selection URIs"
      );
    }

    const auto busPath = directory.file("private-bus");
    PrivateBusAddress isolated(busPath);
    {
      app::FilePicker picker;
      picker.open(11, "Unavailable private test chooser", images, 512);
      result = waitForResult(picker);
      require(
          result.request == 11 && !result.cancelled && !result.error.empty() && result.bytes.empty() && !picker.poll(),
          "A missing portal publishes one request-scoped error through the owning event-loop wake descriptor"
      );
    }
    UnresponsiveBus bus(busPath);
    {
      app::FilePicker picker;
      picker.open(12, "Cancelled private test chooser", images, 512);
      bus.connected();
      picker.cancel();
      result = waitForResult(picker);
      require(
          result.request == 12 && result.cancelled && result.bytes.empty() && result.error.empty(),
          "Cancellation interrupts an in-flight bus connection and returns only its scoped cancellation result"
      );
    }
    const auto destructionStart = std::chrono::steady_clock::now();
    {
      app::FilePicker picker;
      picker.open(13, "Shutdown private test chooser", images, 512);
      bus.connected();
    }
    require(
        std::chrono::steady_clock::now() - destructionStart < 2s,
        "Destroying a picker cancels an unresponsive chooser connection promptly"
    );
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
