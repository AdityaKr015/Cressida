#include "app/desktop_activation.h"
#include "app/single_instance.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
  void require(bool value, const char* reason) {
    if (!value)
      throw std::runtime_error(reason);
  }
  struct Runtime {
    std::filesystem::path path;
    Runtime() {
      char pattern[] = "/tmp/cressida-instance-test-XXXXXX";
      const char* created = ::mkdtemp(pattern);
      require(created, "Create a private test runtime");
      path = created;
    }
    ~Runtime() { std::filesystem::remove_all(path); }
  };
  std::string packet(
      std::string token,
      std::uint64_t timestamp =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
              .count()
  ) {
    return "Cressida.Open/1\n" + std::to_string(timestamp) + '\n' + token;
  }
  void send(const Runtime& runtime, const std::string& data) {
    const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    require(fd >= 0, "Open a test sender");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto path = (runtime.path / "cressida/instance.sock").string();
    require(path.size() < sizeof(address.sun_path), "The fixture socket path fits");
    path.copy(address.sun_path, sizeof(address.sun_path) - 1);
    const auto count = ::sendto(
        fd, data.data(), data.size(), MSG_NOSIGNAL, reinterpret_cast<const sockaddr*>(&address),
        offsetof(sockaddr_un, sun_path) + path.size() + 1
    );
    ::close(fd);
    require(count == static_cast<ssize_t>(data.size()), "Send the complete test activation");
  }
  int wait(pid_t child) {
    int status = 0;
    require(::waitpid(child, &status, 0) == child && WIFEXITED(status), "The child exits normally");
    return WEXITSTATUS(status);
  }
  int launch(const Runtime& runtime, const char* binary, const char* option) {
    const auto child = ::fork();
    require(child >= 0, "Fork a real launcher");
    if (child == 0) {
      ::setenv("XDG_RUNTIME_DIR", runtime.path.c_str(), 1);
      ::setenv("XDG_ACTIVATION_TOKEN", "offline-launch-token", 1);
      ::setenv("XDG_CONFIG_HOME", (runtime.path / "config").c_str(), 1);
      ::setenv("XDG_STATE_HOME", (runtime.path / "state").c_str(), 1);
      ::setenv("XDG_CACHE_HOME", (runtime.path / "cache").c_str(), 1);
      ::setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/cressida-test-no-session-bus", 1);
      ::setenv("WAYLAND_DISPLAY", (runtime.path / "no-wayland-display").c_str(), 1);
      const int sink = ::open("/dev/null", O_WRONLY);
      ::dup2(sink, STDOUT_FILENO);
      ::dup2(sink, STDERR_FILENO);
      ::close(sink);
      // A missing token file prevents any account or graphics startup if handoff regresses.
      if (*option)
        ::execl(binary, binary, option, "--token-file", "/cressida-test-no-such-token-file", nullptr);
      else
        ::execl(binary, binary, "--token-file", "/cressida-test-no-such-token-file", nullptr);
      ::_exit(127);
    }
    return wait(child);
  }
  char readReport(int fd) {
    pollfd descriptor{fd, POLLIN, 0};
    constexpr int deadlineMs = 10000;
    require(::poll(&descriptor, 1, deadlineMs) == 1, "Concurrent startup completes within its deadline");
    char value = 0;
    require(::read(fd, &value, sizeof(value)) == sizeof(value), "Read a startup result");
    return value;
  }
} // namespace
int main(int argc, char** argv) {
  try {
    {
      Runtime runtime;
      app::SingleInstance owner(runtime.path);
      require(owner.claim() == app::SingleInstance::Launch::Owner, "The first launch owns the application");
      require((::fcntl(owner.eventFd(), F_GETFD) & FD_CLOEXEC) != 0, "The activation socket never leaks through exec");
      struct stat info{};
      require(
          ::stat((runtime.path / "cressida/instance.sock").c_str(), &info) == 0 && (info.st_mode & 0777) == 0600,
          "Only the current user can send activation requests"
      );
      app::SingleInstance second(runtime.path);
      require(
          second.claim("desktop-token") == app::SingleInstance::Launch::Forwarded && second.eventFd() < 0,
          "Another launch forwards before starting an application"
      );
      pollfd ready{owner.eventFd(), POLLIN, 0};
      require(::poll(&ready, 1, 0) == 1, "A forwarded Open wakes the native event loop");
      require(owner.poll() == "desktop-token" && !owner.poll(), "Activation tokens are delivered once");
      app::SingleInstance third(runtime.path), fourth(runtime.path);
      require(
          third.claim("latest-token") == app::SingleInstance::Launch::Forwarded
              && fourth.claim() == app::SingleInstance::Launch::Forwarded,
          "Launches coalesce without starting a second owner"
      );
      require(owner.poll() == "latest-token", "An empty request preserves a coalesced valid activation handle");
      send(runtime, "garbage");
      send(runtime, "Cressida.Open/1\nnot-a-timestamp\nsecret");
      send(runtime, packet(std::string(app::DesktopActivationToken::maximumBytes + 256, 'x')));
      require(!owner.poll(), "Malformed and oversized datagrams cannot request activation");
      send(runtime, packet("expired-token", 0));
      require(owner.poll() == "", "An old Open still restores the window without reusing an expired token");
      send(runtime, packet("future-token", std::numeric_limits<std::uint64_t>::max()));
      require(owner.poll() == "", "Future timestamps cannot make an activation token immortal");
      send(runtime, packet(std::string("bad\0token", 9)));
      require(owner.poll() == "", "Embedded NULs never reach Wayland activation");
      const std::string maximum(app::DesktopActivationToken::maximumBytes, 'x');
      app::SingleInstance bounded(runtime.path);
      require(
          bounded.claim(maximum) == app::SingleInstance::Launch::Forwarded && owner.poll() == maximum,
          "The complete bounded activation handle fits in one datagram"
      );
      if (argc > 1) {
        require(
            launch(runtime, argv[1], "") == 0 && owner.poll() == "offline-launch-token",
            "An ordinary command launch activates the owner before reading credentials"
        );
        require(
            launch(runtime, argv[1], "--start-minimized") == 0 && owner.poll() == "offline-launch-token",
            "The real launcher hands off before reading credentials, including start-minimized launches"
        );
        for (const char* option : {"--new-instance", "-n"})
          require(
              launch(runtime, argv[1], option) == 1 && !owner.poll(),
              "Explicit separate launches bypass handoff and continue to their own token validation"
          );
        for (const char* option : {"--demo", "--smoke-test"})
          require(
              launch(runtime, argv[1], option) == 1 && !owner.poll(),
              "Offline previews bypass handoff and reach the private missing-display fixture"
          );
        require(
            launch(runtime, argv[1], "--new-instance=true") == 2 && !owner.poll(),
            "Invalid instance options fail before contacting the owner"
        );
        for (const char* option : {"--help", "--version"})
          require(
              launch(runtime, argv[1], option) == 0 && !owner.poll(), "Help and version never contact another instance"
          );
      }
    }
    {
      Runtime runtime;
      const auto child = ::fork();
      require(child >= 0, "Fork an owner that exits without cleanup");
      if (child == 0) {
        app::SingleInstance owner(runtime.path);
        ::_exit(owner.claim() == app::SingleInstance::Launch::Owner ? 0 : 1);
      }
      require(wait(child) == 0, "The crashed fixture acquired the lock");
      require(std::filesystem::exists(runtime.path / "cressida/instance.sock"), "Abrupt exit leaves a stale socket");
      app::SingleInstance replacement(runtime.path);
      require(
          replacement.claim() == app::SingleInstance::Launch::Owner, "A fresh launch safely replaces a crashed owner"
      );
    }
    {
      Runtime runtime;
      int start[2]{}, reports[2]{}, release[2]{};
      require(::pipe(start) == 0 && ::pipe(reports) == 0 && ::pipe(release) == 0, "Create startup race barriers");
      constexpr unsigned racers = 4;
      pid_t children[racers]{};
      for (auto& child : children) {
        child = ::fork();
        require(child >= 0, "Start concurrent launchers");
        if (child == 0) {
          char go;
          if (::read(start[0], &go, sizeof(go)) != sizeof(go))
            ::_exit(1);
          app::SingleInstance instance(runtime.path);
          const bool owner = instance.claim() == app::SingleInstance::Launch::Owner;
          const char result = owner ? 'O' : 'F';
          if (::write(reports[1], &result, sizeof(result)) != sizeof(result))
            ::_exit(1);
          if (owner && ::read(release[0], &go, sizeof(go)) != sizeof(go))
            ::_exit(1);
          ::_exit(0);
        }
      }
      for (unsigned i = 0; i < racers; ++i)
        require(::write(start[1], "s", 1) == 1, "Release a launcher");
      unsigned owners = 0;
      for (unsigned i = 0; i < racers; ++i) {
        const char result = readReport(reports[0]);
        require(result == 'O' || result == 'F', "Every concurrent launch elects or forwards");
        owners += result == 'O';
      }
      require(owners == 1, "Simultaneous launches create exactly one application owner");
      require(::write(release[1], "s", 1) == 1, "Release the elected owner");
      for (auto child : children)
        require(wait(child) == 0, "All launchers exit cleanly");
      for (int fd : {start[0], start[1], reports[0], reports[1], release[0], release[1]})
        ::close(fd);
    }
    {
      Runtime runtime;
      std::filesystem::create_directory(runtime.path / "cressida");
      ::chmod((runtime.path / "cressida").c_str(), 0700);
      const auto path = runtime.path / "cressida/instance.sock";
      std::ofstream(path) << "preserve this ordinary file";
      bool rejected = false;
      try {
        app::SingleInstance instance(runtime.path);
        instance.claim();
      } catch (const std::exception&) {
        rejected = true;
      }
      std::ifstream file(path);
      std::string content;
      std::getline(file, content);
      require(rejected && content == "preserve this ordinary file", "Stale cleanup never removes an ordinary file");
    }
    for (const bool directory : {true, false}) {
      Runtime runtime, outside;
      std::filesystem::create_directory(runtime.path / "cressida");
      ::chmod((runtime.path / "cressida").c_str(), 0700);
      if (directory) {
        std::filesystem::remove(runtime.path / "cressida");
        std::filesystem::create_directory_symlink(outside.path, runtime.path / "cressida");
      } else
        std::filesystem::create_symlink(outside.path / "untouched", runtime.path / "cressida/instance.lock");
      bool rejected = false;
      try {
        app::SingleInstance instance(runtime.path);
        instance.claim();
      } catch (const std::exception&) {
        rejected = true;
      }
      require(
          rejected && std::filesystem::is_empty(outside.path),
          "Activation startup never follows directory or lock symlinks"
      );
    }
    std::cout << "Single-instance election, activation, stale recovery, race and bootstrap checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
