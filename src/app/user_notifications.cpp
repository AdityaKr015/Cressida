#include "app/client_application.h"
#include "app/message_mentions.h"
#include "app/message_preferences.h"
#include "app/notification_preview.h"

#include <algorithm>

namespace app {
  void ClientApplication::notifyMessage(const api::Message& message, const std::string& channel) {
    if (m_demo
        || !m_connected
        || !m_live
        || !m_preferences.desktopNotifications
        || m_presence.status == "dnd"
        || message.authorId.empty()
        || message.authorId == m_account.id
        || message.deleted
        || (message.flags & 64)
        || m_userDirectory.relationship(message.authorId) == 2
        || m_userDirectory.ignored(message.authorId)
        || readingChannel(channel)
        || channelMuted(channel))
      return;
    if (std::ranges::find(m_notifiedMessages, message.id) != m_notifiedMessages.end())
      return;
    m_notifiedMessages.push_back(message.id);
    if (m_notifiedMessages.size() > 512)
      m_notifiedMessages.erase(m_notifiedMessages.begin());
    std::string guild;
    if (const auto found = m_unreadGuilds.find(channel); found != m_unreadGuilds.end())
      guild = found->second;
    if (guild.empty())
      for (const auto& [id, channels] : m_guildChannels)
        if (std::ranges::find(channels, channel, &api::Channel::id) != channels.end()) {
          guild = id;
          break;
        }
    if (!guild.empty() && m_userDirectory.muted(guild))
      return;
    const auto dm = std::ranges::find(m_privateChannels, channel, &api::Channel::id);
    if (guild.empty() && dm == m_privateChannels.end())
      return;
    const auto roles =
        guild.empty() ? std::optional<std::vector<std::string>>{} : m_roleDirectory.memberships(guild, m_account.id);
    const bool mentioned = messageMentions(
        message, m_account.id, roles ? std::span<const std::string>(*roles) : std::span<const std::string>{}
    );
    int level = m_userDirectory.notificationLevel(channel);
    if (level == 3 && !guild.empty())
      level = m_userDirectory.notificationLevel(guild);
    if (level == 2 || (!guild.empty() && level != 0 && !mentioned))
      return;
    DesktopNotice notice;
    notice.account = m_account.id;
    notice.guild = guild;
    notice.channel = channel;
    notice.message = message.id;
    notice.generation = m_session;
    notice.sound = m_preferences.notificationSounds;
    notice.title = "Cressida";
    notice.body = "You have a new message.";
    if (m_preferences.notificationPrivacy < 2) {
      notice.avatar = message.avatar;
      if (const auto cached = m_images.find(notice.avatar); cached != m_images.end())
        notice.avatarBytes = cached->second.bytes;
      notice.title = userName(message.authorId, message.author);
      if (!guild.empty()) {
        const auto server = std::ranges::find(m_guilds, guild, &api::Guild::id);
        if (server != m_guilds.end())
          notice.title += " · " + server->name;
      }
      if (!m_preferences.notificationPrivacy) {
        const auto guildRoles = m_roleDirectory.allRoles(guild);
        notice.body = desktopMessagePreview(message, [&](char kind, const std::string& id) -> std::string {
          if (kind == '&') {
            const auto role = std::ranges::find(guildRoles, id, &api::ColoredRole::id);
            return role == guildRoles.end() ? "" : role->name;
          }
          if (kind == '#') {
            if (const auto channels = m_guildChannels.find(guild); channels != m_guildChannels.end()) {
              const auto entry = std::ranges::find(channels->second, id, &api::Channel::id);
              if (entry != channels->second.end())
                return entry->name;
            }
            return "";
          }
          const auto mentioned = message.mentions.find(id);
          if (mentioned != message.mentions.end())
            return userName(id, mentioned->second);
          if (id == message.authorId)
            return userName(id, message.author);
          const auto user = lookupUser(id);
          return user.name == "Unknown user" ? "" : userName(id, user.name);
        });
      }
    }
    const auto avatar = notice.avatar;
    const bool needsAvatar = !avatar.empty() && notice.avatarBytes.empty();
    m_desktopNotifications.show(std::move(notice));
    if (needsAvatar)
      (void)m_assets.request(avatar);
  }
  void ClientApplication::drainDesktopNotifications() {
    while (auto action = m_desktopNotifications.poll()) {
      if (!m_connected
          || !m_preferences.desktopNotifications
          || action->generation != m_session
          || action->account != m_account.id)
        continue;
      if (settingsVisible() || !m_dialogKind.empty() || userAccountDialogOpen())
        continue;
      if (action->guild.empty()) {
        if (std::ranges::find(m_privateChannels, action->channel, &api::Channel::id) == m_privateChannels.end())
          continue;
        showDirectMessages();
      } else {
        if (std::ranges::find(m_guilds, action->guild, &api::Guild::id) == m_guilds.end())
          continue;
        selectGuild(action->guild);
      }
      selectChannel(action->channel);
      m_window.invalidate();
    }
  }
} // namespace app
