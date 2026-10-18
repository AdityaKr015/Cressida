#include "app/client_application.h"

#include <algorithm>

namespace app {
  bool ClientApplication::openServerContext(const Hit& target, float x, float y) {
    const auto guild = std::ranges::find(m_guilds, target.id, &api::Guild::id);
    if (guild == m_guilds.end() || m_serverSettings.open)
      return false;
    m_contextEntries.clear();
    m_channelMenuCommands.clear();
    m_channelSubmenuEntries.clear();
    m_reactionChoices.clear();
    m_channelSubmenu = m_reactionSubmenu = m_reactionSubmenuFocus = false;
    m_contextMessage.clear();
    m_contextChannel.clear();
    m_contextUser.clear();
    m_contextGuild = guild->id;
    api::Channel base;
    base.overwrites.emplace();
    const auto permissions = api::channelPermissions(
        guild->id, m_account.id, m_roleDirectory.owner(guild->id), m_roleDirectory.allRoles(guild->id),
        m_roleDirectory.memberships(guild->id, m_account.id), base
    );
    constexpr std::uint64_t management = (1ULL << 1)
        | (1ULL << 2)
        | (1ULL << 3)
        | (1ULL << 4)
        | (1ULL << 5)
        | (1ULL << 7)
        | (1ULL << 19)
        | (1ULL << 22)
        | (1ULL << 23)
        | (1ULL << 24)
        | (1ULL << 27)
        | (1ULL << 28)
        | (1ULL << 29)
        | (1ULL << 30)
        | (1ULL << 33)
        | (1ULL << 40)
        | (1ULL << 43)
        | (1ULL << 44);
    if (permissions.value_or(0) & management)
      m_contextEntries.push_back(
          {.id = static_cast<int>(Action::ServerSettings), .label = "Server Settings", .icon = "settings"}
      );
    if (guild->id == m_guildId && std::ranges::any_of(m_channels, [&](const auto& channel) {
          return channel.type != 4 && !channel.isThread() && (channelPermissionsFor(channel.id).value_or(0) & 1);
        }))
      m_contextEntries.push_back(
          {.id = static_cast<int>(Action::ServerInvitePeople), .label = "Invite People", .icon = "user-plus"}
      );
    if (!m_contextEntries.empty())
      m_contextEntries.push_back({.separator = true});
    m_contextEntries.push_back({.id = static_cast<int>(Action::CopyServerId), .label = "Copy Server ID", .icon = "id"});
    m_contextQuickIndex = -1;
    m_controlInput.setFocus(nullptr);
    m_focusedInput.clear();
    m_contextX = x;
    m_contextY = y;
    m_scrolling.stop(m_contextScroll);
    m_contextScroll = 0;
    m_contextOpen = m_contextDirty = true;
    m_window.invalidate();
    return true;
  }
} // namespace app
