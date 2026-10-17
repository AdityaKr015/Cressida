#pragma once

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

namespace process {

  // A pollable, coalescing cross-thread wake-up primitive. A nonblocking pipe
  // is available on both Linux and FreeBSD, unlike Linux-specific eventfd.
  class WakeEvent {
  public:
    WakeEvent() {
      int descriptors[2] = {-1, -1};
      if (::pipe(descriptors) != 0) {
        return;
      }
      m_readFd = descriptors[0];
      m_writeFd = descriptors[1];
      if (!setFlags(m_readFd) || !setFlags(m_writeFd)) {
        close();
      }
    }

    ~WakeEvent() { close(); }

    WakeEvent(const WakeEvent&) = delete;
    WakeEvent& operator=(const WakeEvent&) = delete;

    [[nodiscard]] bool valid() const noexcept { return m_readFd >= 0 && m_writeFd >= 0; }
    [[nodiscard]] int fd() const noexcept { return m_readFd; }

    [[nodiscard]] bool signal() const noexcept {
      if (!valid()) {
        return false;
      }
      constexpr std::uint8_t byte = 1;
      const ssize_t written = ::write(m_writeFd, &byte, sizeof(byte));
      return written == static_cast<ssize_t>(sizeof(byte)) || (written < 0 && errno == EAGAIN);
    }

    void drain() const noexcept {
      if (!valid()) {
        return;
      }
      std::uint8_t buffer[64];
      while (::read(m_readFd, buffer, sizeof(buffer)) > 0) {
      }
    }

  private:
    static bool setFlags(int fd) noexcept {
      const int descriptorFlags = ::fcntl(fd, F_GETFD);
      const int statusFlags = ::fcntl(fd, F_GETFL);
      return descriptorFlags >= 0
          && statusFlags >= 0
          && ::fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) == 0
          && ::fcntl(fd, F_SETFL, statusFlags | O_NONBLOCK) == 0;
    }

    void close() noexcept {
      if (m_readFd >= 0) {
        ::close(m_readFd);
        m_readFd = -1;
      }
      if (m_writeFd >= 0) {
        ::close(m_writeFd);
        m_writeFd = -1;
      }
    }

    int m_readFd = -1;
    int m_writeFd = -1;
  };

} // namespace process
