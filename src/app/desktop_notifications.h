#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace app {
  struct DesktopNotice {
    std::string account, guild, channel, message, title, body;
    bool sound = true;
    std::uint64_t generation = 0;
  };
  struct DesktopNoticeAction {
    std::string account, guild, channel, message;
    std::uint64_t generation = 0;
  };
  // Session-only desktop delivery, isolated from the application's owning loop.
  class DesktopNotifications {
  public:
    DesktopNotifications();
    ~DesktopNotifications();
    void start();
    void show(DesktopNotice notice);
    void clear();
    int eventFd() const;
    std::optional<DesktopNoticeAction> poll();

  private:
    struct Impl;
    std::unique_ptr<Impl> m;
  };
} // namespace app
