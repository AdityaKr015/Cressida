#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace app {
  struct DesktopNotice {
    std::string account, guild, channel, message, title, body;
    bool sound = true;
    std::uint64_t generation = 0;
    std::string avatar;
    std::vector<std::uint8_t> avatarBytes;
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
    // Complete an existing shared asset request; an empty result uses the app icon.
    void avatarLoaded(const std::string& path, std::span<const std::uint8_t> bytes);
    void clear();
    int eventFd() const;
    std::optional<DesktopNoticeAction> poll();

  private:
    struct Impl;
    std::unique_ptr<Impl> m;
  };
} // namespace app
