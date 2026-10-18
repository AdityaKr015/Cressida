#pragma once
#include <string>

namespace app {
  enum class ChannelMenuKind {
    Read,
    ClearLog,
    Invite,
    Pin,
    CopyLink,
    MuteMenu,
    Mute,
    NotifyMenu,
    Notify,
    MoveMenu,
    Move,
    Edit,
    Duplicate,
    CreateText,
    CreateCategory,
    Delete,
    CopyId,
    HideMuted
  };
  struct ChannelMenuCommand {
    ChannelMenuKind kind;
    std::string guild, channel, argument;
    int value = 0;
  };
  inline constexpr int channelMenuBase = 100000;
} // namespace app
