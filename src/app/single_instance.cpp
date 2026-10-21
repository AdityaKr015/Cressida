#include "app/single_instance.h"

#include "app/desktop_activation.h"

#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace app {
  namespace {
    constexpr char socketName[] = "instance.sock", lockName[] = "instance.lock";
    constexpr std::string_view command = "Cressida.Open/1\n";
    constexpr auto launchDeadline = std::chrono::seconds(5);
    constexpr auto launchRetry = std::chrono::milliseconds(20);
    constexpr std::size_t maximumDatagram =
        command.size() + std::numeric_limits<std::uint64_t>::digits10 + 2 + DesktopActivationToken::maximumBytes;
    constexpr unsigned maximumBatch = 32;
    using Clock = std::chrono::steady_clock;
    std::uint64_t milliseconds() {
      return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    }
    [[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
    bool privateDirectory(int fd) {
      struct stat info{};
      return fd >= 0
          && ::fstat(fd, &info) == 0
          && S_ISDIR(info.st_mode)
          && info.st_uid == ::getuid()
          && (info.st_mode & (S_IRWXG | S_IRWXO)) == 0;
    }
    int openDirectory(const std::filesystem::path& path) {
      const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      if (!privateDirectory(fd)) {
        if (fd >= 0)
          ::close(fd);
        fail("Single-instance startup requires a private, user-owned runtime directory.");
      }
      return fd;
    }
  } // namespace
  SingleInstance::SingleInstance(std::filesystem::path runtime) : m_runtime(std::move(runtime)) {}
  SingleInstance::~SingleInstance() {
    if (m_socket >= 0)
      ::close(m_socket);
    if (m_owner)
      ::unlinkat(m_directory, socketName, 0);
    if (m_lock >= 0)
      ::close(m_lock);
    if (m_directory >= 0)
      ::close(m_directory);
  }
  SingleInstance::Launch SingleInstance::claim(std::string_view activationToken) {
    if (m_directory >= 0)
      fail("Single-instance startup was already initialized.");
    if (m_runtime.empty()) {
      if (const auto* directory = std::getenv("XDG_RUNTIME_DIR"); directory && *directory)
        m_runtime = directory;
      else {
        m_runtime = std::filesystem::path("/run/user") / std::to_string(::getuid());
        if (!std::filesystem::exists(m_runtime)) {
          m_runtime = std::filesystem::path("/tmp") / ("cressida-runtime-" + std::to_string(::getuid()));
          if (::mkdir(m_runtime.c_str(), S_IRWXU) < 0 && errno != EEXIST)
            fail("Could not create the private activation directory.");
        }
      }
    }
    const int runtime = openDirectory(m_runtime);
    if (::mkdirat(runtime, "cressida", S_IRWXU) < 0 && errno != EEXIST) {
      ::close(runtime);
      fail("Could not create the activation directory.");
    }
    m_directory = ::openat(runtime, "cressida", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    ::close(runtime);
    if (!privateDirectory(m_directory))
      fail("The activation directory must be private and owned by the current user.");
    m_lock = ::openat(m_directory, lockName, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR);
    struct stat info{};
    if (m_lock < 0
        || ::fstat(m_lock, &info) < 0
        || !S_ISREG(info.st_mode)
        || info.st_uid != ::getuid()
        || info.st_nlink != 1)
      fail("Could not safely open the application instance lock.");
    m_socket = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_socket < 0)
      fail("Could not open the application activation socket.");
    // Resolve through the retained directory descriptor, avoiding path length and symlink races.
    const auto path = "/proc/self/fd/" + std::to_string(m_directory) + "/" + socketName;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    path.copy(address.sun_path, sizeof(address.sun_path) - 1);
    const auto addressSize = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
    DesktopActivationToken token;
    token.assign(activationToken);
    const auto packet = std::string(command) + std::to_string(milliseconds()) + '\n' + token.take();
    const auto deadline = Clock::now() + launchDeadline;
    do {
      if (::flock(m_lock, LOCK_EX | LOCK_NB) == 0) {
        if (::fstatat(m_directory, socketName, &info, AT_SYMLINK_NOFOLLOW) == 0) {
          if (!S_ISSOCK(info.st_mode) || info.st_uid != ::getuid() || ::unlinkat(m_directory, socketName, 0) < 0)
            fail("Could not safely replace the stale activation socket.");
        } else if (errno != ENOENT)
          fail("Could not inspect the activation socket.");
        if (::bind(m_socket, reinterpret_cast<const sockaddr*>(&address), addressSize) < 0)
          fail("Could not bind the application activation socket.");
        m_owner = true;
        if (::fchmodat(m_directory, socketName, S_IRUSR | S_IWUSR, 0) < 0)
          fail("Could not protect the application activation socket.");
        return Launch::Owner;
      }
      if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR)
        fail("Could not acquire the application instance lock.");
      if (::sendto(
              m_socket, packet.data(), packet.size(), MSG_NOSIGNAL, reinterpret_cast<const sockaddr*>(&address),
              addressSize
          )
          == static_cast<ssize_t>(packet.size()))
        return Launch::Forwarded;
      if (errno != ENOENT && errno != ECONNREFUSED && errno != EAGAIN && errno != EINTR)
        fail("Could not activate the running Cressida instance.");
      ::poll(nullptr, 0, launchRetry.count());
    } while (Clock::now() < deadline);
    fail("The running Cressida instance could not be reached. Use --new-instance to launch separately.");
  }
  std::optional<std::string> SingleInstance::poll() {
    if (!m_owner)
      return {};
    bool open = false;
    DesktopActivationToken token;
    std::array<char, maximumDatagram> bytes{};
    for (unsigned i = 0; i < maximumBatch; ++i) {
      const auto count = ::recv(m_socket, bytes.data(), bytes.size(), MSG_DONTWAIT | MSG_TRUNC);
      if (count < 0) {
        if (errno == EINTR)
          continue;
        break;
      }
      if (static_cast<std::size_t>(count) > bytes.size())
        continue;
      const std::string_view packet(bytes.data(), count);
      if (!packet.starts_with(command))
        continue;
      const auto end = packet.find('\n', command.size());
      if (end == std::string_view::npos)
        continue;
      std::uint64_t sent = 0;
      const auto parsed = std::from_chars(packet.data() + command.size(), packet.data() + end, sent);
      if (parsed.ec != std::errc{} || parsed.ptr != packet.data() + end)
        continue;
      open = true;
      const auto value = packet.substr(end + 1);
      const auto now = milliseconds();
      const auto lifetime =
          std::chrono::duration_cast<std::chrono::milliseconds>(DesktopActivationToken::lifetime).count();
      if (now >= sent && now - sent <= static_cast<std::uint64_t>(lifetime) && !value.empty()) {
        DesktopActivationToken valid;
        if (valid.assign(value))
          token.assign(valid.take());
      }
    }
    return open ? std::optional<std::string>(token.take()) : std::nullopt;
  }
} // namespace app
