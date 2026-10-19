#pragma once
#include <string>

namespace app {
  enum class ServerMenuKind {
    Read,
    Invite,
    MuteMenu,
    Mute,
    NotifyMenu,
    Notify,
    Notifications,
    HideMuted,
    ShowAll,
    SettingsMenu,
    Settings,
    Privacy,
    Profile,
    CreateChannel,
    CreateCategory,
    CreateEvent,
    MoveMenu,
    Move,
    NewFolder,
    Security,
    Leave,
    CopyId,
    FolderRead,
    FolderMoveMenu,
    FolderMove,
    FolderSettings,
    FolderCloseAll
  };
  struct ServerMenuCommand {
    ServerMenuKind kind;
    std::string guild, argument;
    int value = 0;
  };
  inline constexpr int serverMenuBase = 200000;
} // namespace app
