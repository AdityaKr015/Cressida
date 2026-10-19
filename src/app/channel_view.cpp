#include "app/client_application.h"
#include "app/reply_preview.h"
#include "app/theme.h"
#include "ui/controls/button.h"
#include "ui/controls/scrollbar.h"
#include "ui/controls/slider.h"
#include "ui/controls/toggle.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <glib.h>
#include <iostream>
#include <nlohmann/json.hpp>

namespace app {
  using namespace theme;
  namespace {
    bool matches(const std::string& value, const std::string& query) {
      auto* a = g_utf8_casefold(value.c_str(), value.size());
      auto* b = g_utf8_casefold(query.c_str(), query.size());
      const bool result = a && b && std::string_view(a).contains(b);
      g_free(a);
      g_free(b);
      return result;
    }
    std::string freshNonce(std::uint64_t serial) {
      const auto milliseconds =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
              .count();
      return std::to_string((static_cast<std::uint64_t>(milliseconds - 1420070400000LL) << 22) | (serial & 0x3FFFFF));
    }
  } // namespace
  const api::Channel* ClientApplication::selectedChannel() const {
    const auto channel = std::ranges::find(m_channels, m_channelId, &api::Channel::id);
    return channel == m_channels.end() ? nullptr : &*channel;
  }
  std::optional<std::uint64_t> ClientApplication::selectedChannelPermissions() const {
    const auto* channel = selectedChannel();
    if (m_dmMode || !channel)
      return {};
    return api::channelPermissions(
        m_guildId, m_account.id, m_roleDirectory.owner(m_guildId), m_roleDirectory.allRoles(m_guildId),
        m_roleDirectory.memberships(m_guildId, m_account.id), *channel
    );
  }
  void ClientApplication::closeChannelPanel() {
    m_channelPanel.clear();
    m_channelPanelError.clear();
    m_channelPageRequest.reset();
    m_channelPageShown.reset();
    m_channelPage = {};
    m_pinCursors.clear();
    m_scrolling.stop(m_channelPanelScroll);
    m_channelPanelScroll = 0;
    m_window.invalidate();
  }
  void ClientApplication::clearChannelFeatures() {
    clearChatPreload();
    m_channelMuteTimer.stop();
    m_channelListPreferences = {};
    m_channelMenuCommands.clear();
    m_channelSubmenuEntries.clear();
    m_channelSubmenu = false;
    m_sidebarInviteUrl.clear();
    closeChannelPanel();
    m_channelSearchText.clear();
    m_seekMessage.clear();
    m_channelDetailRequests.clear();
    m_channelDetailsLoaded.clear();
    m_channelDetailErrors.clear();
    m_componentStates.clear();
    m_dismissedEphemeral.clear();
    m_deletedChannels.clear();
    m_pendingMessageJump.reset();
    m_scrolling.stop(m_channelDialogScroll);
    m_channelDialogScroll = 0;
    m_channelFormName.clear();
    m_channelFormTopic.clear();
    m_channelAccessFilter.clear();
    m_channelAccessTarget.clear();
    m_channelEditOriginal = {};
  }
  void ClientApplication::filterDeletedChannels(api::Event& event) const {
    std::erase_if(event.channels, [&](const auto& channel) {
      return m_deletedChannels.contains(channel.id)
          || (channel.isThread() && m_deletedChannels.contains(channel.parentId));
    });
  }
  void ClientApplication::removeGuildChannels(const api::Event& event) {
    if (!api::validSnowflake(event.resource))
      return;
    bool selected = false;
    const auto* thread = m_threads.find(m_guildId, m_channelId);
    auto removed = event.removed;
    for (const auto& child : m_threads.list(event.resource))
      if (std::ranges::find(event.removed, child.parentId) != event.removed.end())
        removed.push_back(child.id);
    if (m_contextOpen && std::ranges::find(removed, m_contextChannel) != removed.end())
      closeContext();
    if ((m_dialogAction && std::ranges::find(removed, m_dialogAction->channel) != removed.end())
        || (m_dialogKind.starts_with("channel-")
            && std::ranges::find(removed, m_channelEditOriginal.id) != removed.end()))
      closeDialog();
    if (event.resource == m_guildId) {
      const auto old = m_channelListPreferences.pinned.size();
      std::erase_if(m_channelListPreferences.pinned, [&](const auto& id) {
        return std::ranges::find(removed, id) != removed.end();
      });
      if (old != m_channelListPreferences.pinned.size() && !m_demo)
        m_sessionStore.saveChannelList(m_account.id, m_guildId, m_channelListPreferences);
    }
    for (const auto& id : removed) {
      if (!api::validSnowflake(id))
        continue;
      if (id == m_chatPreloadChannel || id == m_chatPreloadHover)
        clearChatPreload();
      m_deletedChannels.insert(id);
      selected |= !m_dmMode && event.resource == m_guildId && (m_channelId == id || (thread && thread->parentId == id));
      m_channelMessages.erase(id);
      m_channelHistoryState.erase(id);
      m_messageLog.clearChannel(id);
      m_channelMembers.erase(id);
      std::erase(m_channelCacheOrder, id);
      m_channelDetailRequests.erase(id);
      m_channelDetailsLoaded.erase(id);
      m_channelDetailErrors.erase(id);
      m_threadRequests.erase(id);
      m_threadsLoaded.erase(id);
      m_threadErrors.erase(id);
      m_threadPages.erase(id);
      m_forumFirstMessages.erase(id);
      m_unreadChannels.erase(id);
      m_unreadDMs.erase(id);
      m_unreadGuilds.erase(id);
      m_localRead.erase(id);
      std::erase_if(m_lastChannels, [&](const auto& item) { return item.second == id; });
      if (m_restoreChannel == id)
        m_restoreChannel.clear();
    }
    const auto deleted = [&](const auto& channel) {
      return m_deletedChannels.contains(channel.id)
          || (channel.isThread() && m_deletedChannels.contains(channel.parentId));
    };
    const auto unparent = [&](auto& channels) {
      for (auto& channel : channels)
        if (!channel.isThread() && std::ranges::find(removed, channel.parentId) != removed.end())
          channel.parentId.clear();
      channels = api::orderChannels(std::move(channels));
    };
    if (auto base = m_guildChannels.find(event.resource); base != m_guildChannels.end()) {
      std::erase_if(base->second, deleted);
      unparent(base->second);
    }
    if (!m_dmMode && event.resource == m_guildId) {
      m_loadingChannels = false;
      std::erase_if(m_channels, deleted);
      unparent(m_channels);
      rebuildThreadChannels();
      if (selected) {
        const auto fallback = std::ranges::find_if(m_channels, [](const auto& channel) {
          return channel.type != 4 && !channel.isThread();
        });
        selectChannel(fallback == m_channels.end() ? "" : fallback->id);
        if (!m_demo && m_channelId.empty())
          m_client.subscribe({}, {});
      } else if (m_deletedChannels.contains(m_threadParent)) {
        closeDialog();
        m_threadParent.clear();
      }
    }
    while (m_deletedChannels.size() > 4096)
      m_deletedChannels.erase(m_deletedChannels.begin());
    m_window.invalidate();
  }
  void ClientApplication::drawChannelHeader(Node& root, float w) {
    const float left = chatLeft();
    const float width = w - left;
    rectangle(root, left, 36, width, 66, background);
    const auto* channel = selectedChannel();
    if (!channel) {
      label(root, left + 24, 57, width - 48, "Choose a channel", 19, foreground, FontWeight::SemiBold);
      return;
    }
    const bool thread = channel->isThread();
    const float searchWidth = width >= 690 ? std::clamp(width * 0.28F, 180.0F, 260.0F) : 32;
    const float searchX = w - 18 - searchWidth;
    if (searchWidth > 32)
      inputControl(root, searchX, 52, searchWidth, 32, "channel-search", m_channelSearchText, "Search messages…");
    else
      iconButton(
          root, {searchX, 52, 32, 32, Action::ChannelSearch, {}}, "search", "Search messages (Ctrl+F)",
          m_channelPanel == "search"
      );
    float icons = searchX - 146;
    iconButton(
        root,
        {icons, 52, 30, 32, m_dmMode ? Action::ChannelInfo : Action::BrowseThreads,
         thread ? channel->parentId : channel->id},
        m_dmMode ? "info-circle" : "messages", m_dmMode ? "Conversation details" : "Threads"
    );
    iconButton(
        root, {icons + 35, 52, 30, 32, Action::ToggleChannelMute, {}},
        m_userDirectory.muted(m_channelId) ? "bell-off" : "bell",
        m_userDirectory.muted(m_channelId) ? "Unmute channel" : "Mute channel", m_userDirectory.muted(m_channelId)
    );
    iconButton(
        root, {icons + 70, 52, 30, 32, Action::ChannelPins, {}}, "pin", "Pinned messages", m_channelPanel == "pins"
    );
    iconButton(
        root, {icons + 105, 52, 30, 32, Action::ToggleMembers, {}}, "users", "Member list",
        m_showMembers && m_channelPanel.empty()
    );
    float nameX = left + 22;
    if (thread) {
      iconButton(
          root, {left + 13, 52, 28, 32, Action::ThreadParent, channel->parentId}, "arrow-left", "Back to channel"
      );
      nameX += 28;
    } else if (m_dmMode && channel->type == 1 && !channel->recipients.empty()) {
      statusBadge(root, nameX + 5, 68, userStatus(channel->recipients.front().id), background);
      nameX += 23;
    } else if (!m_dmMode) {
      glyph(root, nameX, 58, 21, channel->isForum() ? "messages" : "hash", muted);
      nameX += 31;
    }
    const float available = std::max(20.0F, icons - nameX - 12);
    const auto name = channelName(*channel);
    const auto measured = m_window.target().renderer().measureText(name, 16, FontWeight::SemiBold);
    const float nameWidth = std::min(available, std::max(40.0F, measured.width + 4));
    label(root, nameX, 60, nameWidth, name, 16, foreground, FontWeight::SemiBold);
    m_hits.push_back({nameX, 48, nameWidth, 44, Action::ChannelInfo, {}});
    float topicX = nameX + nameWidth + 18;
    if (thread && available - nameWidth > 96) {
      const auto action = channel->archived.value_or(false) ? Action::ReopenThread
          : channel->joined.value_or(false)                 ? Action::LeaveThread
                                                            : Action::JoinThread;
      button(
          root, {topicX, 54, 80, 29, m_threadPending.contains(channel->id) ? Action::None : action, channel->id},
          m_threadPending.contains(channel->id) ? "Saving…"
              : action == Action::ReopenThread  ? "Reopen"
              : action == Action::LeaveThread   ? "Leave"
                                                : "Join"
      );
    } else if (!m_dmMode && channel->topic && !channel->topic->empty() && icons - topicX > 75) {
      rectangle(root, topicX - 9, 56, 1, 25, raised);
      const auto measure = [this](std::string_view text, const MessageStyle& style) {
        return m_window.target()
            .renderer()
            .measureText(
                messageMarkup(text, style), style.size, FontWeight::Normal, 0, 1, TextAlign::Start, {},
                TextEllipsize::End, true
            )
            .width;
      };
      const auto preview = layoutMessage(
          oneLineReplyText(*channel->topic), 16384, measure, 12, {}, {.compact = true, .discordTimestamps = true}
      );
      auto topic = std::make_unique<Node>();
      topic->setPosition(topicX, 58);
      topic->setSize(icons - topicX - 12, 24);
      topic->setClipChildren(true);
      for (const auto& run : preview.runs) {
        if (run.y > 3)
          continue;
        if (run.style.spoiler)
          rectangle(*topic, run.x, 2, run.width, run.height, raised, 3);
        else if (!run.image.empty())
          emojiImage(*topic, run.x, 0, run.width, run.image);
        else
          label(
              *topic, run.x, 2, run.width + 2, messageMarkup(run.text, run.style), run.style.size, muted,
              FontWeight::Normal, 1, true
          );
      }
      root.addChild(std::move(topic));
      m_hits.push_back({topicX, 48, std::max(1.0F, icons - topicX - 12), 44, Action::ChannelInfo, {}});
    }
  }
  float ClientApplication::channelIntro(Node* root, float x, float y, float width) {
    const auto* channel = selectedChannel();
    if (!channel
        || m_dmMode
        || channel->isThread()
        || channel->isForum()
        || m_loadingMessages
        || !(
            m_demo ? m_demoOverlay == "channel"
                    || m_demoOverlay.starts_with("channel-empty")
                    || m_demoOverlay == "channel-cycle"
                    || m_demoOverlay == "channel-edit"
                    || m_demoOverlay == "channel-search"
                   : m_history.initialized() && m_history.olderEnd()
        ))
      return 0;
    const auto roles = m_roleDirectory.allRoles(m_guildId);
    const auto access = api::channelAccessRoles(m_guildId, roles, *channel);
    const auto base = api::channelPermissions(m_guildId, "", "", roles, std::vector<std::string>{}, *channel);
    const bool privateChannel = base && !(*base & api::viewChannel);
    const auto permissions = selectedChannelPermissions().value_or(0);
    const auto measure = [&](std::string_view text, const MessageStyle& style) {
      return m_window.target()
          .renderer()
          .measureText(
              messageMarkup(text, style), style.size, FontWeight::Normal, 0, 1, TextAlign::Start, {},
              TextEllipsize::End, true
          )
          .width;
    };
    const auto description = layoutMessage(
        channel->topic.value_or(""), width, measure, 13, {},
        {.compact = true,
         .unicodeEmoji = [this](std::string_view value) { return m_emojiCatalog.isUnicode(value); },
         .discordTimestamps = true}
    );
    const float topicHeight = std::min(180.0F, description.height);
    const bool controls = permissions & (api::manageChannel | api::managePermissions);
    const float controlsY = 142 + (channel->topic && !channel->topic->empty() ? topicHeight + 14 : 0);
    const float chipsY = controlsY + (controls ? 42 : 0);
    struct Chip {
      const api::ColoredRole* role;
      float x, y, width;
    };
    std::vector<Chip> chips;
    float cx = 0, cy = 0;
    if (privateChannel)
      for (const auto& role : access) {
        if (role.id == m_guildId)
          continue;
        const float size = std::min(width, m_window.target().renderer().measureText(role.name, 11).width + 34);
        if (cx > 0 && cx + size > width) {
          cx = 0;
          cy += 30;
        }
        if (cy >= 60)
          break;
        chips.push_back({&role, cx, cy, size});
        cx += size + 7;
      }
    const float chipHeight = chips.empty() ? 0 : chips.back().y + 30;
    const float height = chipsY + chipHeight + 30;
    if (!root)
      return height;
    rectangle(*root, x, y + 4, 58, 58, panel, 29);
    glyph(*root, x + 12, y + 16, 34, privateChannel ? "lock" : "hash", foreground);
    label(*root, x, y + 78, width, "Welcome to #" + channel->name + "!", 24, foreground, FontWeight::Bold);
    label(
        *root, x, y + 117, width,
        "This is the start of the #" + channel->name + (privateChannel ? " private channel." : " channel."), 12, muted
    );
    auto hitsBegin = m_hits.size();
    for (const auto& run : description.runs) {
      if (run.y + run.height > topicHeight)
        continue;
      if (run.style.spoiler)
        rectangle(*root, x + run.x, y + 142 + run.y, run.width, run.height, raised, 3);
      else if (!run.image.empty())
        emojiImage(*root, x + run.x, y + 142 + run.y, run.width, run.image);
      else
        label(
            *root, x + run.x, y + 142 + run.y, run.width + 2, messageMarkup(run.text, run.style), run.style.size,
            run.style.link.empty() ? foreground : accent, FontWeight::Normal, 1, true
        );
      if (!run.style.link.empty() && !run.style.spoiler)
        m_hits.push_back(
            {chatLeft() + 24 + x + run.x, m_messageViewportTop + y + 142 + run.y, run.width, run.height,
             Action::OpenUrl, run.style.link}
        );
    }
    float controlX = x;
    if (permissions & api::managePermissions) {
      button(*root, {controlX, y + controlsY, 181, 30, Action::AddChannelAccess, {}}, "Add members or roles");
      controlX += 191;
    }
    if (permissions & api::manageChannel)
      button(*root, {controlX, y + controlsY, 112, 30, Action::EditChannel, {}}, "Edit Channel");
    // button() accepts node-local coordinates; convert only these intro hits.
    for (std::size_t i = hitsBegin; i < m_hits.size(); ++i) {
      auto& h = m_hits[i];
      const float end = std::min(m_messageViewportBottom, h.y + h.height);
      h.y = std::max(m_messageViewportTop, h.y);
      h.height = std::max(0.0F, end - h.y);
    }
    for (const auto& chip : chips) {
      rectangle(*root, x + chip.x, y + chipsY + chip.y, chip.width, 24, panel, 7);
      rectangle(
          *root, x + chip.x + 9, y + chipsY + chip.y + 8, 9, 9,
          chip.role->colors.primary ? rgbHex(chip.role->colors.primary) : muted, 5
      );
      label(*root, x + chip.x + 25, y + chipsY + chip.y + 7, chip.width - 30, chip.role->name, 11, foreground);
    }
    rectangle(*root, x, y + height - 12, width, 1, raised);
    return height;
  }
  void ClientApplication::channelFeatureEvent(const api::Event& event) {
    if (event.operation == api::Operation::ChannelDetails) {
      m_channelDetailRequests.erase(event.resource);
      if (!event.error.empty())
        m_channelDetailErrors[event.resource] = event.error;
      else {
        m_channelDetailsLoaded.insert(event.resource);
        m_channelDetailErrors.erase(event.resource);
        for (const auto& fresh : event.channels) {
          if (m_channelEditOriginal.id == fresh.id) {
            if (m_dialogKind == "channel-info")
              m_channelEditOriginal = fresh;
            else if (!m_dialogPending && (m_dialogKind == "channel-edit" || m_dialogKind == "channel-access")) {
              if (m_channelFormName == m_channelEditOriginal.name)
                m_channelFormName = fresh.name;
              if (m_channelFormTopic == m_channelEditOriginal.topic.value_or(""))
                m_channelFormTopic = fresh.topic.value_or("");
              if (m_channelFormNsfw == m_channelEditOriginal.nsfw.value_or(false))
                m_channelFormNsfw = fresh.nsfw.value_or(false);
              if (m_channelFormRateLimit == m_channelEditOriginal.rateLimitPerUser.value_or(0))
                m_channelFormRateLimit = fresh.rateLimitPerUser.value_or(0);
              m_channelEditOriginal = fresh;
            }
          }
          for (auto& [guild, channels] : m_guildChannels)
            for (auto& old : channels)
              if (old.id == fresh.id)
                old = fresh;
          for (auto& old : m_channels)
            if (old.id == fresh.id)
              old = fresh;
        }
        if (!m_demo && !m_dmMode && event.resource == m_channelId)
          refreshMemberSubscription();
        if (m_contextOpen
            && !m_channelMenuCommands.empty()
            && std::ranges::any_of(event.channels, [&](const auto& channel) { return channel.id == m_contextChannel; }))
          openChannelContext({0, 0, 0, 0, Action::ChannelMenu, m_contextChannel}, m_contextX, m_contextY);
      }
    } else if (event.channelQuery && m_channelPageRequest == event.channelQuery) {
      m_channelPageRequest.reset();
      m_channelPanelError = event.error;
      if (event.channelPage && event.error.empty()) {
        m_channelPage = *event.channelPage;
        m_channelPageShown = event.channelQuery;
        m_scrolling.stop(m_channelPanelScroll);
        m_channelPanelScroll = 0;
      }
    }
    m_window.invalidate();
  }
  void ClientApplication::requestChannelPage(bool next, bool previous) {
    if (m_demo || m_channelPageRequest || m_channelPanel.empty() || m_channelId.empty())
      return;
    api::ChannelPageQuery query;
    query.kind = m_channelPanel == "pins" ? api::ChannelPage::Pins : api::ChannelPage::Search;
    query.guild = m_guildId;
    query.channel = m_channelId;
    query.text = m_channelSearchText;
    query.request = ++m_channelFeatureSerial;
    if (m_channelPageShown && m_channelPageShown->kind == query.kind && (next || previous)) {
      query = *m_channelPageShown;
      query.request = m_channelFeatureSerial;
      if (query.kind == api::ChannelPage::Search)
        query.offset = next ? query.offset + 25 : query.offset >= 25 ? query.offset - 25 : 0;
      else if (next) {
        m_pinCursors.push_back(query.before);
        query.before = m_channelPage.before;
      } else if (!m_pinCursors.empty()) {
        query.before = m_pinCursors.back();
        m_pinCursors.pop_back();
      }
    }
    m_channelPanelError.clear();
    if (m_client.channelPage(query))
      m_channelPageRequest = query;
    else
      m_channelPanelError = query.kind == api::ChannelPage::Search && query.text.empty()
          ? "Enter a search, then press Enter."
          : "Could not load messages. Try again.";
    m_window.invalidate();
  }
  void ClientApplication::searchChannel() {
    if (m_channelId.empty())
      return;
    closeChannelPanel();
    m_channelPanel = "search";
    requestChannelPage();
    m_window.invalidate();
  }
  void ClientApplication::drawChannelPanel(Node& root, float x, float y, float width, float height) {
    rectangle(root, x, y, width, height, sidebar);
    rectangle(root, x, y, 1, height, raised);
    label(
        root, x + 18, y + 19, width - 74, m_channelPanel == "pins" ? "Pinned messages" : "Search results", 18,
        foreground, FontWeight::SemiBold
    );
    iconButton(root, {x + width - 44, y + 10, 32, 32, Action::CloseChannelPanel, {}}, "x", "Close panel");
    float top = y + 60;
    if (m_channelPanel == "search") {
      inputControl(root, x + 16, top, width - 32, 34, "channel-search-panel", m_channelSearchText, "Search messages…");
      top += 46;
      if (m_channelPageShown)
        label(root, x + 18, top, width - 36, std::to_string(m_channelPage.total) + " results", 11, muted);
      top += 24;
    }
    const float footer = !m_channelPanelError.empty() ? 112 : 58, viewport = std::max(20.0F, y + height - top - footer);
    constexpr float rowHeight = 138;
    const float fullHeight = m_channelPage.messages.size() * rowHeight;
    m_scrolling.limit(m_channelPanelScroll, std::max(0.0F, fullHeight - viewport));
    auto content = std::make_unique<Node>();
    content->setPosition(x + 12, top);
    content->setSize(width - 24, viewport);
    content->setClipChildren(true);
    content->setTextPixelSnap(false);
    for (std::size_t i = 0; i < m_channelPage.messages.size(); ++i) {
      const auto& message = m_channelPage.messages[i];
      const float ry = i * rowHeight - m_channelPanelScroll;
      if (ry + rowHeight <= 0 || ry >= viewport)
        continue;
      const auto rowTop = std::max(top, top + ry), rowEnd = std::min(top + viewport, top + ry + rowHeight - 10);
      rectangle(
          *content, 0, ry, width - 30, rowHeight - 10,
          hovered(x + 12, rowTop, width - 30, rowEnd - rowTop) ? raised : panel, 9
      );
      label(
          *content, 12, ry + 13, width - 56, userName(message.authorId, message.author), 13, foreground,
          FontWeight::SemiBold
      );
      const auto channel = std::ranges::find(m_channels, message.channelId, &api::Channel::id);
      label(
          *content, 12, ry + 35, width - 56,
          (channel == m_channels.end() ? "Channel" : "#" + channel->name) + " · " + message.timestamp.substr(0, 10), 10,
          muted
      );
      auto text = oneLineReplyText(message.content);
      if (text.empty())
        text = !message.attachments.empty() ? "Attachment" : "Message";
      label(*content, 12, ry + 58, width - 56, text, 12, foreground, FontWeight::Normal, 3);
      m_hits.push_back(
          {x + 12, rowTop, width - 30, std::max(0.0F, rowEnd - rowTop), Action::ChannelPanelJump,
           message.channelId + ":" + message.id}
      );
    }
    if (m_channelPage.messages.empty())
      label(
          *content, 8, 22, width - 42,
          m_channelPageRequest               ? "Loading…"
              : !m_channelPanelError.empty() ? ""
              : m_channelPanel == "pins"     ? "No pinned messages."
                                             : "Enter a search above.",
          13, muted
      );
    root.addChild(std::move(content));
    if (fullHeight > viewport) {
      const std::string id = "channel-panel-scrollbar";
      std::unique_ptr<Node> owned;
      if (auto old = m_detachedInputs.find(id); old != m_detachedInputs.end()) {
        owned = std::move(old->second);
        m_detachedInputs.erase(old);
      } else {
        auto bar = std::make_unique<Scrollbar>();
        bar->setOnScrollChanged([this](float v) {
          m_scrolling.stop(m_channelPanelScroll);
          m_channelPanelScroll = v;
          m_window.invalidate();
        });
        owned = std::move(bar);
      }
      auto* bar = static_cast<Scrollbar*>(owned.get());
      m_widgets[id] = bar;
      bar->setPosition(x + width - 13, top);
      bar->setSize(bar->reservedThickness(), viewport);
      bar->update(viewport, fullHeight, m_channelPanelScroll);
      root.addChild(std::move(owned));
    }
    const float fy = top + viewport + 12;
    if (!m_channelPanelError.empty()) {
      label(root, x + 16, fy, width - 32, m_channelPanelError, 12, rgbHex(0xED4245), FontWeight::Normal, 3);
      button(root, {x + 16, y + height - 42, 80, 30, Action::ChannelPanelRetry, {}}, "Retry");
    } else if (m_channelPageRequest)
      label(root, x + 18, fy, width - 36, "Loading…", 12, muted);
    else {
      if (m_channelPageShown && (m_channelPageShown->offset > 0 || !m_pinCursors.empty()))
        button(root, {x + 16, fy, 90, 30, Action::ChannelPanelPrevious, {}}, "Previous");
      if (m_channelPage.more)
        button(root, {x + width - 106, fy, 90, 30, Action::ChannelPanelNext, {}}, "Next");
    }
  }
  void ClientApplication::openChannelDialog(const std::string& kind, const std::string& id) {
    const auto at = std::ranges::find(m_channels, id, &api::Channel::id);
    const auto* channel = id.empty() ? selectedChannel() : at == m_channels.end() ? nullptr : &*at;
    if (!channel)
      return;
    const auto copy = *channel;
    closeDialog();
    closeContext();
    m_emojiMenu = m_presenceMenu = false;
    closeGifPicker();
    m_dialogKind = kind;
    m_dialogError.clear();
    m_dialogPending = false;
    m_channelEditOriginal = copy;
    m_channelFormName = copy.name;
    m_channelFormTopic = copy.topic.value_or("");
    m_channelFormNsfw = copy.nsfw.value_or(false);
    m_channelFormRateLimit = copy.rateLimitPerUser.value_or(0);
    const auto permissions = channelPermissionsFor(copy.id).value_or(0);
    m_channelEditPage = kind == "channel-access" ? "access"
        : kind == "channel-edit" && !(permissions & api::manageChannel) && (permissions & api::managePermissions)
        ? "permissions"
        : "overview";
    m_channelPermissionTarget = m_guildId;
    m_channelPermissionType = 0;
    m_channelPermissionDraft.clear();
    m_channelPermissionPending.clear();
    m_channelReturnToPermissions = false;
    m_channelAccessFilter.clear();
    m_channelAccessTarget.clear();
    m_channelAccessMode = 0;
    m_scrolling.stop(m_channelDialogScroll);
    m_channelDialogScroll = 0;
    if (kind == "channel-edit")
      m_focusedInput = m_channelEditPage == "overview" ? "channel-name" : "";
    else if (kind == "channel-access")
      m_focusedInput = "channel-access-filter";
    m_window.invalidate();
  }
  bool ClientApplication::channelEditClose() {
    if (m_dialogKind != "channel-edit" && m_dialogKind != "channel-access")
      return false;
    if (m_dialogPending)
      return true;
    const bool category = m_channelEditOriginal.type == 4;
    const bool dirty = m_channelFormName != m_channelEditOriginal.name
        || (!category && m_channelFormTopic != m_channelEditOriginal.topic.value_or(""))
        || (!category && m_channelFormNsfw != m_channelEditOriginal.nsfw.value_or(false))
        || (!category && m_channelFormRateLimit != m_channelEditOriginal.rateLimitPerUser.value_or(0))
        || !m_channelPermissionDraft.empty();
    if (!dirty)
      return false;
    m_dialogError = "Save or reset your changes before closing.";
    m_window.invalidate();
    return true;
  }
  void ClientApplication::drawChannelDialog(Node& root, float w, float h) {
    if (m_dialogKind == "channel-edit" || m_dialogKind == "channel-access") {
      const bool access = m_dialogKind == "channel-access" || m_channelEditPage == "access";
      const bool permissions = access || m_channelEditPage == "permissions";
      const bool category = m_channelEditOriginal.type == 4;
      const auto capabilities = channelPermissionsFor(m_channelEditOriginal.id).value_or(0);
      const bool canManage = (capabilities & api::manageChannel) != 0;
      const bool canPermissions = (capabilities & api::managePermissions) != 0;
      const bool changed = m_channelFormName != m_channelEditOriginal.name
          || (!category && m_channelFormTopic != m_channelEditOriginal.topic.value_or(""))
          || (!category && m_channelFormNsfw != m_channelEditOriginal.nsfw.value_or(false))
          || (!category && m_channelFormRateLimit != m_channelEditOriginal.rateLimitPerUser.value_or(0))
          || !m_channelPermissionDraft.empty();
      const float navWidth = w < 700 ? 150.0F : 200.0F;
      const float navX = std::max(16.0F, (w - 1040) / 2);
      const float navRight = navX + navWidth;
      const float contentX = navRight + (w < 700 ? 20 : 40);
      const float width = std::max(170.0F, std::min(660.0F, w - contentX - (w < 700 ? 44 : 86)));
      const bool stackedFooter = width < 440;
      const float top = 112,
                  footer = changed || access || !m_dialogError.empty() ? (stackedFooter ? 112.0F : 84.0F) : 28.0F;
      const float viewport = std::max(80.0F, h - top - footer);
      rectangle(root, 0, 36, w, h - 36, background);
      rectangle(root, 0, 36, navRight, h - 36, sidebar);
      m_dialogBounds = {0, 36, w, h - 36, Action::None, {}};
      m_hits.push_back(m_dialogBounds);
      label(
          root, navX + 8, 60, navWidth - 16,
          (category                              ? ""
               : m_channelEditOriginal.type == 2 ? ""
                                                 : "# ")
              + m_channelEditOriginal.name,
          12, muted, FontWeight::SemiBold
      );
      const auto editButton = [&](Node& parent, Hit hit, const std::string& text, bool primary = false,
                                  bool selected = false, bool = false) {
        const std::string controlId =
            "channel-edit:button:" + std::to_string(static_cast<int>(hit.action)) + ":" + hit.id + ":" + text;
        std::unique_ptr<Node> saved;
        if (auto found = m_detachedInputs.find(controlId); found != m_detachedInputs.end()) {
          saved = std::move(found->second);
          m_detachedInputs.erase(found);
        }
        auto item = saved ? std::unique_ptr<Button>(static_cast<Button*>(saved.release())) : std::make_unique<Button>();
        m_widgets[controlId] = item.get();
        const bool navigation = (hit.action == Action::ChannelEditPage && (&parent == &root))
            || hit.action == Action::ChannelPermissionTarget;
        item->setText(text);
        item->setFontSize(12);
        item->setVariant(
            primary                                 ? ButtonVariant::Primary
                : selected                          ? ButtonVariant::TabActive
                : navigation                        ? ButtonVariant::Tab
                : text == "Reset" || text == "Back" ? ButtonVariant::Ghost
                                                    : ButtonVariant::Outline
        );
        item->setContentAlign(navigation ? ButtonContentAlign::Start : ButtonContentAlign::Center);
        item->setPosition(hit.x, hit.y + (hit.height - 26) / 2);
        item->setControlHeight(26);
        item->setRadius(5);
        item->setPadding(0, 8);
        item->setFrameSize(hit.width, 26);
        item->setEnabled(hit.action != Action::None);
        item->setOnClick([this, hit] { activate(hit); });
        parent.addChild(std::move(item));
        float x = 0, y = 0;
        for (auto* node = &parent; node && node != m_scene.get(); node = node->parent()) {
          x += node->x();
          y += node->y();
        }
        hit.x += x;
        hit.y += y;
        m_hits.push_back(std::move(hit));
      };
      editButton(
          root, {navX, 93, navWidth - 12, 32, m_dialogPending ? Action::None : Action::ChannelEditPage, "overview"},
          "Overview", false, !permissions, !permissions
      );
      if (canPermissions)
        editButton(
            root,
            {navX, 130, navWidth - 12, 32, m_dialogPending ? Action::None : Action::ChannelEditPage, "permissions"},
            "Permissions", false, permissions, permissions
        );
      label(
          root, contentX, 65, width,
          access            ? "Add members or roles"
              : permissions ? "Permissions"
                            : "Overview",
          21, foreground, FontWeight::SemiBold
      );
      const float closeX = std::min(w - 40, contentX + width + 24);
      iconButton(
          root, {closeX, 58, 30, 30, m_dialogPending ? Action::None : Action::CloseDialog, {}}, "circle-x",
          "Close settings"
      );
      label(root, closeX + 3, 93, 30, "ESC", 10, muted);

      auto clipped = std::make_unique<Node>();
      clipped->setPosition(contentX, top);
      clipped->setSize(width, viewport);
      clipped->setClipChildren(true);
      clipped->setTextPixelSnap(false);
      auto* clip = root.addChild(std::move(clipped));
      auto contents = std::make_unique<Node>();
      contents->setPosition(0, -m_channelDialogScroll);
      contents->setSize(width, 1);
      auto* body = clip->addChild(std::move(contents));
      const auto firstHit = m_hits.size();
      float cursor = 0;
      const auto textHeight = [&](const std::string& text, float size, float textWidth,
                                  FontWeight weight = FontWeight::Normal) {
        const auto bounds = m_window.target().renderer().measureText(
            text, size, weight, std::max(40.0F, textWidth), 0, TextAlign::Start, {}, TextEllipsize::End
        );
        return std::max(size * 1.3F, std::ceil(bounds.bottom - bounds.top));
      };
      const auto detail = [&](const std::string& text, float y, float textWidth) {
        label(*body, 0, y, textWidth, text, 12, muted, FontWeight::Normal, 0);
        return textHeight(text, 12, textWidth);
      };
      const auto toggle = [&](const std::string& id, bool enabled, float y, const std::string& command, bool allowed) {
        std::unique_ptr<Node> saved;
        if (auto found = m_detachedInputs.find(id); found != m_detachedInputs.end()) {
          saved = std::move(found->second);
          m_detachedInputs.erase(found);
        }
        auto control =
            saved ? std::unique_ptr<Toggle>(static_cast<Toggle*>(saved.release())) : std::make_unique<Toggle>();
        m_widgets[id] = control.get();
        control->setToggleSize(ToggleSize::Small);
        control->setCheckedImmediate(enabled);
        control->setEnabled(allowed && !m_dialogPending);
        control->setPosition(width - 30, y);
        control->setFrameSize(30, 18);
        control->setOnChange([this, command](bool) { activate({0, 0, 0, 0, Action::ChannelEditOption, command}); });
        body->addChild(std::move(control));
      };
      const auto triState = [&](const std::string& target, int type, std::uint64_t bit) {
        if (target == m_channelPermissionTarget && type == m_channelPermissionType)
          if (auto value = m_channelPermissionDraft.find(bit); value != m_channelPermissionDraft.end())
            return value->second;
        if (m_channelEditOriginal.overwrites)
          for (const auto& overwrite : *m_channelEditOriginal.overwrites)
            if (overwrite.id == target && overwrite.type == type) {
              if (api::permissionBits(overwrite.deny).value_or(0) & bit)
                return -1;
              if (api::permissionBits(overwrite.allow).value_or(0) & bit)
                return 1;
            }
        return 0;
      };
      if (!permissions) {
        label(
            *body, 0, cursor, width, category ? "Category Name" : "Channel Name", 13, foreground, FontWeight::SemiBold
        );
        inputControl(
            *body, 0, cursor + 25, width, 38, "channel-name", m_channelFormName,
            category ? "New category" : "new-channel"
        )
            .setEnabled(canManage && !m_dialogPending);
        cursor += 90;
        if (!category) {
          label(
              *body, 0, cursor, width, m_channelEditOriginal.isForum() ? "Post Guidelines" : "Channel Topic", 13,
              foreground, FontWeight::SemiBold
          );
          inputControl(
              *body, 0, cursor + 25, width, 150, "channel-topic", m_channelFormTopic,
              m_channelEditOriginal.isForum() ? "Help members create great posts…"
                                              : "Let everyone know how to use this channel!",
              true
          )
              .setEnabled(canManage && !m_dialogPending);
          // Native Input scales chrome with its control height; keep multiline corners compact.
          m_inputs.at("channel-topic")->setFrameRadius(5 * Style::controlHeight / 150);
          const auto limit = m_channelEditOriginal.isForum() ? 4096 : 1024;
          label(
              *body, width - 100, cursor + 183, 100,
              std::to_string(api::utf8Length(m_channelFormTopic)) + " / " + std::to_string(limit), 11, muted
          );
          cursor += 225;
          if (m_channelEditOriginal.type != 2) {
            label(*body, 0, cursor, width - 95, "Slowmode", 13, foreground, FontWeight::SemiBold);
            static constexpr std::array<int, 14> durations{0,   5,   10,  15,   30,   60,   120,
                                                           300, 600, 900, 1800, 3600, 7200, 21600};
            const auto durationName = [](int value) {
              if (!value)
                return std::string("Off");
              if (value < 60)
                return std::to_string(value) + " seconds";
              if (value < 3600)
                return std::to_string(value / 60) + (value == 60 ? " minute" : " minutes");
              return std::to_string(value / 3600) + (value == 3600 ? " hour" : " hours");
            };
            label(*body, width - 95, cursor + 1, 95, durationName(m_channelFormRateLimit), 12, muted);
            std::unique_ptr<Node> saved;
            const std::string id = "channel-edit:slowmode";
            if (auto found = m_detachedInputs.find(id); found != m_detachedInputs.end()) {
              saved = std::move(found->second);
              m_detachedInputs.erase(found);
            }
            auto control =
                saved ? std::unique_ptr<Slider>(static_cast<Slider*>(saved.release())) : std::make_unique<Slider>();
            m_widgets[id] = control.get();
            const auto at = std::ranges::lower_bound(durations, m_channelFormRateLimit);
            control->setOnValueChanged({});
            control->setRange(0, durations.size() - 1);
            control->setStep(1);
            control->setValue(std::min<std::size_t>(at - durations.begin(), durations.size() - 1));
            control->setEnabled(canManage && !m_dialogPending);
            control->setPosition(0, cursor + 27);
            control->setFrameSize(width, 28);
            control->setOnValueChanged([this](double value) {
              static constexpr std::array<int, 14> values{0,   5,   10,  15,   30,   60,   120,
                                                          300, 600, 900, 1800, 3600, 7200, 21600};
              m_channelFormRateLimit = values[std::clamp(static_cast<int>(value), 0, 13)];
              m_window.invalidate();
            });
            body->addChild(std::move(control));
            cursor += 65;
            cursor += detail(
                          m_channelEditOriginal.isForum()
                              ? "Members can create one post per interval. Members with Bypass Slowmode are exempt."
                              : "Members can send one message per interval. Members with Bypass Slowmode are exempt.",
                          cursor, width
                      )
                + 30;
          }
          label(*body, 0, cursor, width - 50, "Age-Restricted Channel", 13, foreground, FontWeight::SemiBold);
          toggle("channel-edit:nsfw", m_channelFormNsfw, cursor, "nsfw", canManage);
          cursor += 25;
          cursor +=
              detail("Members must confirm they are at least 18 years old to view this channel.", cursor, width - 45)
              + 24;
        }
      } else if (access) {
        editButton(
            *body, {0, cursor, 88, 30, m_dialogPending ? Action::None : Action::ChannelAccessMode, "roles"}, "Roles",
            false, m_channelAccessMode == 0, m_channelAccessMode == 0
        );
        editButton(
            *body, {98, cursor, 104, 30, m_dialogPending ? Action::None : Action::ChannelAccessMode, "members"},
            "Members", false, m_channelAccessMode == 1, m_channelAccessMode == 1
        );
        inputControl(
            *body, 0, cursor + 46, width, 38, "channel-access-filter", m_channelAccessFilter,
            m_channelAccessMode == 0 ? "Search roles" : "Search members or enter a user ID"
        )
            .setEnabled(canPermissions && !m_dialogPending);
        cursor += 105;
        std::vector<std::pair<std::string, std::string>> targets;
        if (!m_channelAccessMode) {
          for (const auto& role : m_roleDirectory.allRoles(m_guildId))
            if (role.id != m_guildId && (m_channelAccessFilter.empty() || matches(role.name, m_channelAccessFilter)))
              targets.emplace_back(role.id, role.name);
        } else {
          for (const auto& member : m_members)
            if (!member.user.id.empty()) {
              const auto name = userName(member.user.id, member.user.name);
              if ((m_channelAccessFilter.empty()
                   || matches(name, m_channelAccessFilter)
                   || member.user.id == m_channelAccessFilter)
                  && std::ranges::none_of(targets, [&](const auto& target) { return target.first == member.user.id; }))
                targets.emplace_back(member.user.id, name);
            }
          if (api::validSnowflake(m_channelAccessFilter) && std::ranges::none_of(targets, [&](const auto& target) {
                return target.first == m_channelAccessFilter;
              }))
            targets.emplace_back(
                m_channelAccessFilter, userName(m_channelAccessFilter, "User " + m_channelAccessFilter)
            );
        }
        for (const auto& [id, name] : targets) {
          const bool selected = m_channelAccessTarget == id;
          rectangle(*body, 0, cursor, width, 40, selected ? raised : panel, 5);
          glyph(*body, 13, cursor + 11, 18, selected ? "circle-check" : "circle", selected ? accent : muted);
          label(*body, 43, cursor + 11, width - 55, name, 13, foreground);
          m_hits.push_back(
              {contentX, top + cursor - m_channelDialogScroll, width, 40,
               m_dialogPending ? Action::None : Action::ChannelAccessTarget, id}
          );
          cursor += 46;
        }
        if (targets.empty())
          cursor += detail("No matching roles or members.", cursor, width) + 30;
      } else {
        const bool privateChannel = triState(m_guildId, 0, api::viewChannel) == -1;
        label(*body, 0, cursor, width - 46, "Private Channel", 14, foreground, FontWeight::SemiBold);
        toggle("channel-edit:private", privateChannel, cursor, "private", canPermissions);
        cursor += 26;
        cursor += detail("Only selected members and roles will be able to view this channel.", cursor, width - 45) + 24;
        const auto accessRoles =
            api::channelAccessRoles(m_guildId, m_roleDirectory.allRoles(m_guildId), m_channelEditOriginal);
        label(*body, 0, cursor, width, "Who can access this channel", 13, foreground, FontWeight::SemiBold);
        cursor += 27;
        if (!privateChannel) {
          label(*body, 0, cursor, width, "@everyone", 13, foreground);
          cursor += 31;
        }
        for (const auto& role : accessRoles) {
          if (role.id == m_guildId)
            continue;
          rectangle(*body, 0, cursor, 8, 8, role.colors.primary ? rgbHex(role.colors.primary) : muted, 4);
          label(*body, 20, cursor - 4, width - 20, role.name, 13, foreground);
          cursor += 28;
        }
        if (m_channelEditOriginal.overwrites)
          for (const auto& overwrite : *m_channelEditOriginal.overwrites)
            if (overwrite.type == 1 && (api::permissionBits(overwrite.allow).value_or(0) & api::viewChannel)) {
              glyph(*body, 0, cursor - 3, 16, "user", muted);
              label(*body, 23, cursor - 3, width - 23, userName(overwrite.id, "User " + overwrite.id), 13, foreground);
              cursor += 28;
            }
        editButton(
            *body,
            {0, cursor + 3, std::min(210.0F, width), 32, m_dialogPending ? Action::None : Action::ChannelEditPage,
             "access"},
            "Add members or roles"
        );
        cursor += 69;
        label(*body, 0, cursor, width, "Advanced permissions", 15, foreground, FontWeight::SemiBold);
        cursor += 30;
        struct PermissionTarget {
          std::string id, name;
          int type;
        };
        std::vector<PermissionTarget> targets{{m_guildId, "@everyone", 0}};
        if (m_channelEditOriginal.overwrites)
          for (const auto& overwrite : *m_channelEditOriginal.overwrites) {
            if (overwrite.id == m_guildId || std::ranges::any_of(targets, [&](const auto& target) {
                  return target.id == overwrite.id && target.type == overwrite.type;
                }))
              continue;
            std::string name = overwrite.type ? userName(overwrite.id, "User " + overwrite.id) : "Role " + overwrite.id;
            if (!overwrite.type)
              for (const auto& role : m_roleDirectory.allRoles(m_guildId))
                if (role.id == overwrite.id)
                  name = role.name;
            targets.push_back({overwrite.id, name, overwrite.type});
          }
        const bool columns = width >= 530;
        const float targetWidth = columns ? 154.0F : width;
        float targetY = cursor;
        for (const auto& target : targets) {
          const bool selected = target.id == m_channelPermissionTarget && target.type == m_channelPermissionType;
          editButton(
              *body,
              {0, targetY, targetWidth, 30, m_dialogPending ? Action::None : Action::ChannelPermissionTarget,
               (target.type ? "member:" : "role:") + target.id},
              target.name, false, selected, selected
          );
          targetY += 35;
        }
        if (!columns)
          cursor = targetY + 20;
        const float permissionX = columns ? targetWidth + 24 : 0, permissionWidth = width - permissionX;
        struct PermissionRow {
          std::uint64_t bit;
          const char* name;
          const char* description;
          bool text;
        };
        static constexpr PermissionRow rows[]{
            {1ULL << 10, "View Channel", "Allows members to view this channel.", false},
            {1ULL << 4, "Manage Channel", "Allows members to edit and delete this channel.", false},
            {1ULL << 28, "Manage Permissions", "Allows members to change channel permissions.", false},
            {1ULL << 29, "Manage Webhooks", "Allows members to create, edit and delete webhooks.", false},
            {1ULL << 0, "Create Invite", "Allows members to invite others to this channel.", false},
            {1ULL << 11, "Send Messages", "Allows members to send messages in this channel.", true},
            {1ULL << 38, "Send Messages in Threads", "Allows members to send messages in this channel's threads.",
             true},
            {1ULL << 35, "Create Public Threads", "Allows members to create threads that everyone can view.", true},
            {1ULL << 36, "Create Private Threads", "Allows members to create invite-only threads.", true},
            {1ULL << 14, "Embed Links", "Shows embedded previews for links sent by members.", true},
            {1ULL << 15, "Attach Files", "Allows members to upload images and files.", true},
            {1ULL << 6, "Add Reactions", "Allows members to add new emoji reactions to messages.", true},
            {1ULL << 18, "Use External Emoji", "Allows members to use emoji from other servers.", true},
            {1ULL << 37, "Use External Stickers", "Allows members to use stickers from other servers.", true},
            {1ULL << 17, "Mention @everyone, @here and All Roles",
             "Allows members to notify everyone and mention any role.", true},
            {1ULL << 13, "Manage Messages", "Allows members to delete messages and pin messages.", true},
            {1ULL << 34, "Manage Threads", "Allows members to manage threads and forum posts.", true},
            {1ULL << 16, "Read Message History", "Allows members to read previous messages.", true},
            {1ULL << 12, "Send Text-to-Speech Messages", "Allows members to send /tts messages.", true},
            {1ULL << 31, "Use Application Commands", "Allows members to use app commands in this channel.", true},
            {1ULL << 46, "Send Voice Messages", "Allows members to send recorded voice messages.", true},
            {1ULL << 49, "Send Polls", "Allows members to create polls.", true},
            {1ULL << 20, "Connect", "Allows members to connect to this voice channel.", false},
            {1ULL << 21, "Speak", "Allows members to speak in this voice channel.", false},
            {1ULL << 9, "Video", "Allows members to stream video and share their screen.", false},
            {1ULL << 22, "Mute Members", "Allows members to mute other members in this voice channel.", false},
            {1ULL << 23, "Deafen Members", "Allows members to deafen other members in this voice channel.", false},
            {1ULL << 24, "Move Members", "Allows members to move members into another voice channel.", false},
            {1ULL << 25, "Use Voice Activity", "Allows members to speak without using Push to Talk.", false}
        };
        for (const auto& permission : rows) {
          if (permission.bit >= (1ULL << 20)
              && (permission.bit
                  & ((1ULL << 20) | (1ULL << 21) | (1ULL << 22) | (1ULL << 23) | (1ULL << 24) | (1ULL << 25)))
              && !category
              && m_channelEditOriginal.type != 2)
            continue;
          if (permission.bit == (1ULL << 9) && !category && m_channelEditOriginal.type != 2)
            continue;
          const float titleWidth = std::max(70.0F, permissionWidth - 112);
          const float heading = textHeight(permission.name, 13, titleWidth, FontWeight::SemiBold);
          label(*body, permissionX, cursor + 5, titleWidth, permission.name, 13, foreground, FontWeight::SemiBold, 0);
          label(
              *body, permissionX, cursor + heading + 13, permissionWidth, permission.description, 12, muted,
              FontWeight::Normal, 0
          );
          const int value = triState(m_channelPermissionTarget, m_channelPermissionType, permission.bit);
          const float controlsX = width - 102;
          rectangle(*body, controlsX, cursor + 2, 102, 30, panel, 4);
          for (int state = -1; state <= 1; ++state) {
            const float rx = controlsX + (state + 1) * 34;
            const Color selectedColor = state < 0 ? destructive : state > 0 ? success : raised;
            if (value == state)
              rectangle(*body, rx + 1, cursor + 3, 32, 28, selectedColor, 3);
            glyph(
                *body, rx + 8, cursor + 9, 16,
                state < 0       ? "x"
                    : state > 0 ? "check"
                                : "slash",
                value == state && state ? rgbHex(0xFFFFFF) : muted
            );
            m_hits.push_back(
                {contentX + rx, top + cursor + 2 - m_channelDialogScroll, 34, 30,
                 canPermissions && !m_dialogPending ? Action::ChannelEditOption : Action::None,
                 "permission:" + std::to_string(permission.bit) + ":" + std::to_string(state)}
            );
          }
          cursor += heading + 26 + textHeight(permission.description, 12, permissionWidth) + 20;
        }
        cursor = std::max(cursor, targetY);
      }
      const float previous = m_channelDialogScroll;
      m_scrolling.limit(m_channelDialogScroll, std::max(0.0F, cursor - viewport));
      body->setPosition(0, -m_channelDialogScroll);
      body->setSize(width, cursor);
      for (std::size_t i = firstHit; i < m_hits.size(); ++i) {
        auto& hit = m_hits[i];
        hit.y += previous - m_channelDialogScroll;
        const float bottom = std::min(top + viewport, hit.y + hit.height);
        hit.y = std::max(top, hit.y);
        hit.height = std::max(0.0F, bottom - hit.y);
      }
      if (cursor > viewport)
        settingsScrollbar(
            root, "channel-edit:scrollbar", {contentX + width + 8, top, 8, viewport, Action::None, {}}, cursor,
            m_channelDialogScroll
        );
      if (changed || access || !m_dialogError.empty()) {
        const float saveY = h - (stackedFooter ? 94 : 66), buttonY = saveY + (stackedFooter ? 30 : 0);
        rectangle(root, contentX - 8, saveY - 7, width + 16, stackedFooter ? 81 : 53, panel, 7);
        if (!m_dialogError.empty())
          label(
              root, contentX + 9, saveY + 9, stackedFooter ? width - 18 : std::max(70.0F, width - 235), m_dialogError,
              12, danger, FontWeight::Normal, 2
          );
        else if (!access)
          label(
              root, contentX + 9, saveY + 10, stackedFooter ? width - 18 : std::max(70.0F, width - 235),
              "Careful — you have unsaved changes!", 12, foreground
          );
        const float resetWidth = access ? 64 : 55, saveWidth = access ? 120 : 118;
        editButton(
            root,
            {contentX + width - saveWidth - resetWidth - 12, buttonY, resetWidth, 34,
             m_dialogPending ? Action::None
                 : access    ? Action::ChannelEditPage
                             : Action::ResetChannelEdit,
             access ? "permissions" : ""},
            access ? "Back" : "Reset"
        );
        const bool enabled = !m_dialogPending && (access ? canPermissions && !m_channelAccessTarget.empty() : changed);
        editButton(
            root,
            {contentX + width - saveWidth,
             buttonY,
             saveWidth,
             34,
             enabled ? access ? Action::SaveChannelAccess : Action::SaveChannelEdit : Action::None,
             {}},
            m_dialogPending ? "Saving…"
                : access    ? "Add Access"
                            : "Save Changes",
            true
        );
      }
      m_tooltip.clear();
      return;
    }

    const float width = std::min(560.0F, w - 32), height = std::min(530.0F, h - 64);
    const float x = (w - width) / 2, y = std::max(44.0F, (h - height) / 2);
    rectangle(root, 0, 36, w, h - 36, Color{0, 0, 0, 0.55F});
    m_hits.push_back({0, 36, w, h - 36, Action::None, {}});
    rectangle(root, x, y, width, height, background, 10);
    m_dialogBounds = {x, y, width, height, Action::None, {}};
    m_hits.push_back(m_dialogBounds);
    label(root, x + 24, y + 23, width - 90, "# " + m_channelEditOriginal.name, 20, foreground, FontWeight::SemiBold);
    iconButton(root, {x + width - 44, y + 15, 30, 30, Action::CloseDialog, {}}, "x", "Close");
    const auto measure = [&](std::string_view value, const MessageStyle& style) {
      return m_window.target()
          .renderer()
          .measureText(
              messageMarkup(value, style), style.size, FontWeight::Normal, 0, 1, TextAlign::Start, {},
              TextEllipsize::End, true
          )
          .width;
    };
    const auto description = layoutMessage(
        m_channelEditOriginal.topic.value_or(""), width - 60, measure, 14, {},
        {.compact = true, .discordTimestamps = true}
    );
    const auto roles = api::channelAccessRoles(m_guildId, m_roleDirectory.allRoles(m_guildId), m_channelEditOriginal);
    const float viewport = height - 150, full = std::max(30.0F, description.height) + 48 + roles.size() * 28;
    m_scrolling.limit(m_channelDialogScroll, std::max(0.0F, full - viewport));
    auto content = std::make_unique<Node>();
    content->setPosition(x + 24, y + 74);
    content->setSize(width - 48, viewport);
    content->setClipChildren(true);
    for (const auto& run : description.runs) {
      const float ry = run.y - m_channelDialogScroll;
      if (run.style.spoiler)
        rectangle(*content, run.x, ry, run.width, run.height, raised, 3);
      else if (!run.image.empty())
        emojiImage(*content, run.x, ry, run.width, run.image);
      else
        label(
            *content, run.x, ry, run.width + 2, messageMarkup(run.text, run.style), run.style.size, foreground,
            FontWeight::Normal, 1, true
        );
    }
    if (m_channelEditOriginal.topic.value_or("").empty())
      label(*content, 0, -m_channelDialogScroll, width - 60, "No channel description.", 14, muted);
    const float roleY = std::max(30.0F, description.height) + 22 - m_channelDialogScroll;
    label(*content, 0, roleY, width - 60, "Roles with channel access", 12, muted, FontWeight::SemiBold);
    for (std::size_t i = 0; i < roles.size(); ++i) {
      const float ry = roleY + 28 + i * 28;
      rectangle(*content, 2, ry + 4, 9, 9, roles[i].colors.primary ? rgbHex(roles[i].colors.primary) : muted, 5);
      label(*content, 22, ry + 1, width - 84, roles[i].name, 12, foreground);
    }
    root.addChild(std::move(content));
    const auto permissions = channelPermissionsFor(m_channelEditOriginal.id).value_or(0);
    if (permissions & (api::manageChannel | api::managePermissions))
      button(root, {x + 24, y + height - 52, 112, 32, Action::EditChannel, {}}, "Edit Channel");
    if (permissions & api::managePermissions)
      button(root, {x + 146, y + height - 52, 181, 32, Action::AddChannelAccess, {}}, "Add members or roles");
    if (m_channelDetailErrors.contains(m_channelEditOriginal.id)) {
      label(root, x + 24, y + 54, width - 144, m_channelDetailErrors[m_channelEditOriginal.id], 11, muted);
      button(root, {x + width - 111, y + 55, 80, 28, Action::ChannelDetailsRetry, {}}, "Retry");
    }
    m_tooltip.clear();
  }
  bool ClientApplication::channelFeatureAction(const Hit& hit) {
    const auto permissions =
        channelPermissionsFor(m_dialogKind.starts_with("channel-") ? m_channelEditOriginal.id : m_channelId)
            .value_or(0);
    switch (hit.action) {
    case Action::DismissEphemeral:
      dismissEphemeral(hit.id);
      break;
    case Action::SubmitApplicationModal:
      submitApplicationModal();
      break;
    case Action::ChannelEditPage:
      if (m_dialogPending)
        break;
      if (hit.id == "access" && !m_channelPermissionDraft.empty()) {
        m_dialogError = "Save or reset your permission changes before adding access.";
        break;
      }
      if (m_dialogKind == "channel-access")
        m_dialogKind = "channel-edit";
      m_channelEditPage = hit.id;
      m_channelReturnToPermissions = hit.id == "access";
      m_dialogError.clear();
      m_focusedInput = hit.id == "overview" ? "channel-name" : hit.id == "access" ? "channel-access-filter" : "";
      m_scrolling.stop(m_channelDialogScroll);
      m_channelDialogScroll = 0;
      break;
    case Action::ResetChannelEdit:
      if (m_dialogPending)
        break;
      m_channelFormName = m_channelEditOriginal.name;
      m_channelFormTopic = m_channelEditOriginal.topic.value_or("");
      m_channelFormNsfw = m_channelEditOriginal.nsfw.value_or(false);
      m_channelFormRateLimit = m_channelEditOriginal.rateLimitPerUser.value_or(0);
      m_channelPermissionDraft.clear();
      m_channelPermissionPending.clear();
      m_dialogError.clear();
      break;
    case Action::ChannelPermissionTarget: {
      if (m_dialogPending)
        break;
      if (!m_channelPermissionDraft.empty()) {
        m_dialogError = "Save or reset your changes before selecting another role or member.";
        break;
      }
      const auto separator = hit.id.find(':');
      if (separator == std::string::npos || !api::validSnowflake(hit.id.substr(separator + 1)))
        break;
      m_channelPermissionTarget = hit.id.substr(separator + 1);
      m_channelPermissionType = hit.id.starts_with("member:") ? 1 : 0;
      m_dialogError.clear();
      break;
    }
    case Action::ChannelEditOption: {
      if (m_dialogPending)
        break;
      if (hit.id == "nsfw") {
        if (permissions & api::manageChannel)
          m_channelFormNsfw = !m_channelFormNsfw;
        break;
      }
      if (!(permissions & api::managePermissions))
        break;
      std::uint64_t bit = api::viewChannel;
      int state = 0;
      if (hit.id == "private") {
        if (!m_channelPermissionDraft.empty() && (m_channelPermissionTarget != m_guildId || m_channelPermissionType)) {
          m_dialogError = "Save or reset your changes before changing channel privacy.";
          break;
        }
        m_channelPermissionTarget = m_guildId;
        m_channelPermissionType = 0;
        int current = 0;
        if (m_channelEditOriginal.overwrites)
          for (const auto& overwrite : *m_channelEditOriginal.overwrites)
            if (overwrite.id == m_guildId && overwrite.type == 0)
              current = (api::permissionBits(overwrite.deny).value_or(0) & api::viewChannel) ? -1 : 0;
        if (auto draft = m_channelPermissionDraft.find(api::viewChannel); draft != m_channelPermissionDraft.end())
          current = draft->second;
        state = current == -1 ? 0 : -1;
      } else if (hit.id.starts_with("permission:")) {
        const auto separator = hit.id.find(':', 11);
        if (separator == std::string::npos)
          break;
        const auto bitText = std::string_view(hit.id).substr(11, separator - 11);
        const auto stateText = std::string_view(hit.id).substr(separator + 1);
        const auto a = std::from_chars(bitText.data(), bitText.data() + bitText.size(), bit);
        const auto b = std::from_chars(stateText.data(), stateText.data() + stateText.size(), state);
        if (a.ec != std::errc{}
            || a.ptr != bitText.data() + bitText.size()
            || b.ec != std::errc{}
            || b.ptr != stateText.data() + stateText.size()
            || !bit
            || (bit & (bit - 1))
            || state < -1
            || state > 1)
          break;
      } else
        break;
      int original = 0;
      if (m_channelEditOriginal.overwrites)
        for (const auto& overwrite : *m_channelEditOriginal.overwrites)
          if (overwrite.id == m_channelPermissionTarget && overwrite.type == m_channelPermissionType) {
            if (api::permissionBits(overwrite.deny).value_or(0) & bit)
              original = -1;
            else if (api::permissionBits(overwrite.allow).value_or(0) & bit)
              original = 1;
          }
      if (state == original)
        m_channelPermissionDraft.erase(bit);
      else
        m_channelPermissionDraft[bit] = state;
      m_dialogError.clear();
      break;
    }
    case Action::ChannelInfo:
      openChannelDialog("channel-info");
      break;
    case Action::EditChannel:
      if (permissions & (api::manageChannel | api::managePermissions))
        openChannelDialog("channel-edit", m_dialogKind.starts_with("channel-") ? m_channelEditOriginal.id : "");
      break;
    case Action::AddChannelAccess:
      if (permissions & api::managePermissions) {
        if (m_dialogKind == "channel-edit") {
          m_channelEditPage = "access";
          m_channelReturnToPermissions = true;
          m_scrolling.stop(m_channelDialogScroll);
          m_channelDialogScroll = 0;
        } else
          openChannelDialog("channel-access", m_dialogKind.starts_with("channel-") ? m_channelEditOriginal.id : "");
      }
      break;
    case Action::ChannelAccessMode:
      m_channelAccessMode = hit.id == "roles" ? 0 : 1;
      m_channelAccessTarget.clear();
      m_channelAccessFilter.clear();
      m_scrolling.stop(m_channelDialogScroll);
      m_channelDialogScroll = 0;
      break;
    case Action::ChannelAccessTarget:
      m_channelAccessTarget = hit.id;
      break;
    case Action::SaveChannelEdit:
    case Action::SaveChannelAccess: {
      if (m_dialogPending
          || std::ranges::find(m_channels, m_channelEditOriginal.id, &api::Channel::id) == m_channels.end())
        break;
      api::Interaction action{
          .kind = hit.action == Action::SaveChannelEdit ? api::InteractionKind::EditChannel
                                                        : api::InteractionKind::GrantChannelAccess,
          .channel = m_channelEditOriginal.id,
          .guild = m_guildId,
          .revision = ++m_channelFeatureSerial
      };
      if (hit.action == Action::SaveChannelEdit) {
        if (!(permissions & api::manageChannel)
            && (m_channelFormName != m_channelEditOriginal.name
                || m_channelFormTopic != m_channelEditOriginal.topic.value_or("")
                || m_channelFormNsfw != m_channelEditOriginal.nsfw.value_or(false)
                || m_channelFormRateLimit != m_channelEditOriginal.rateLimitPerUser.value_or(0)))
          break;
        if (m_channelFormName != m_channelEditOriginal.name)
          action.channelName = m_channelFormName;
        if (m_channelEditOriginal.type != 4 && m_channelFormTopic != m_channelEditOriginal.topic.value_or(""))
          action.channelTopic = m_channelFormTopic;
        if (m_channelEditOriginal.type != 4 && m_channelFormNsfw != m_channelEditOriginal.nsfw.value_or(false))
          action.channelNsfw = m_channelFormNsfw;
        if (m_channelEditOriginal.type != 4
            && m_channelFormRateLimit != m_channelEditOriginal.rateLimitPerUser.value_or(0))
          action.channelRateLimit = m_channelFormRateLimit;
        if (!m_channelPermissionDraft.empty() && !(permissions & api::managePermissions)) {
          m_dialogError = "You no longer have permission to change this channel's permissions.";
          break;
        }
        m_channelPermissionPending = m_channelPermissionDraft;
        if (!action.channelName && !action.channelTopic && !action.channelNsfw && !action.channelRateLimit) {
          if (m_channelPermissionPending.empty())
            break;
          action.kind = api::InteractionKind::EditChannelPermission;
          action.user = m_channelPermissionTarget;
          action.overwriteType = m_channelPermissionType;
          action.permissionBit = m_channelPermissionPending.begin()->first;
          action.permissionValue = m_channelPermissionPending.begin()->second;
        }
        if (action.channelTopic && !m_channelEditOriginal.isForum() && api::utf8Length(m_channelFormTopic) > 1024) {
          m_dialogError = "Channel descriptions can contain up to 1,024 characters.";
          break;
        }
      } else {
        if (!(permissions & api::managePermissions))
          break;
        if (!api::validSnowflake(m_channelAccessTarget)) {
          m_dialogError = "Choose a role or member first.";
          break;
        }
        action.user = m_channelAccessTarget;
        action.overwriteType = m_channelAccessMode;
      }
      if (startInteraction(action)) {
        m_dialogPending = true;
        m_dialogAction = action;
        m_dialogError.clear();
      } else
        m_dialogError = m_status;
      break;
    }
    case Action::ChannelDetailsRetry:
      if (!m_demo && !m_channelDetailRequests.contains(m_channelId) && m_client.channelDetails(m_channelId))
        m_channelDetailRequests.insert(m_channelId);
      break;
    case Action::ToggleChannelMute:
      if (!m_channelId.empty())
        startInteraction(
            {.kind = m_userDirectory.muted(m_channelId) ? api::InteractionKind::Unmute : api::InteractionKind::Mute,
             .channel = m_channelId,
             .guild = m_guildId}
        );
      break;
    case Action::ChannelPins:
      if (m_channelPanel == "pins")
        closeChannelPanel();
      else {
        closeChannelPanel();
        m_channelPanel = "pins";
        requestChannelPage();
      }
      break;
    case Action::ChannelSearch:
      if (m_channelPanel == "search")
        closeChannelPanel();
      else {
        m_channelPanel = "search";
        m_focusedInput = "channel-search-panel";
      }
      break;
    case Action::CloseChannelPanel:
      closeChannelPanel();
      break;
    case Action::ChannelPanelNext:
      requestChannelPage(true);
      break;
    case Action::ChannelPanelPrevious:
      requestChannelPage(false, true);
      break;
    case Action::ChannelPanelRetry:
      requestChannelPage();
      break;
    case Action::ChannelPanelJump: {
      const auto separator = hit.id.find(':');
      if (separator != std::string::npos)
        jumpToMessage(hit.id.substr(0, separator), hit.id.substr(separator + 1));
      break;
    }
    case Action::MessageButton: {
      const auto newline = hit.id.find('\n'), colon = hit.id.find(':', newline);
      if (newline == std::string::npos || colon == std::string::npos)
        break;
      const auto found = std::ranges::find(m_messages, hit.id.substr(0, newline), &api::Message::id);
      std::size_t row = 0, column = 0;
      const auto a = std::from_chars(hit.id.data() + newline + 1, hit.id.data() + colon, row),
                 b = std::from_chars(hit.id.data() + colon + 1, hit.id.data() + hit.id.size(), column);
      if (found == m_messages.end()
          || a.ec != std::errc{}
          || b.ec != std::errc{}
          || row >= found->buttons.size()
          || column >= found->buttons[row].size())
        break;
      const auto& button = found->buttons[row][column];
      if (button.disabled)
        break;
      if (button.style == 5) {
        if (api::externalLink(button.url))
          auth::openExternalUrl(button.url);
        break;
      }
      const auto key = found->id + ":" + button.customId;
      if (m_componentStates.contains(key) && m_componentStates[key].pending)
        break;
      const auto nonce = freshNonce(++m_channelFeatureSerial);
      api::Interaction action{
          .kind = api::InteractionKind::MessageButton,
          .channel = m_channelId,
          .message = found->id,
          .guild = m_guildId,
          .application = found->applicationId,
          .customId = button.customId,
          .nonce = nonce,
          .messageFlags = found->flags
      };
      if (startInteraction(action))
        m_componentStates[key] = {nonce, "Sending…", true};
      else
        m_componentStates[key] = {nonce, m_status, false};
      while (m_componentStates.size() > 128)
        m_componentStates.erase(m_componentStates.begin());
      break;
    }
    default:
      return false;
    }
    m_window.invalidate();
    return true;
  }
  bool ClientApplication::channelFeatureInteraction(const api::Event& event, bool dialog) {
    const auto& action = *event.interaction;
    if (action.kind != api::InteractionKind::EditChannel
        && action.kind != api::InteractionKind::GrantChannelAccess
        && action.kind != api::InteractionKind::EditChannelPermission)
      return false;
    for (const auto& fresh : event.channels) {
      for (auto& [guild, channels] : m_guildChannels)
        for (auto& channel : channels)
          if (channel.id == fresh.id)
            channel = fresh;
      for (auto& channel : m_channels)
        if (channel.id == fresh.id)
          channel = fresh;
    }
    if (event.channels.empty() && !m_demo)
      m_client.channelDetails(action.channel);
    if (dialog && (m_dialogKind == "channel-edit" || m_dialogKind == "channel-access")) {
      if (const auto fresh = std::ranges::find(m_channels, action.channel, &api::Channel::id);
          fresh != m_channels.end()) {
        const bool nameDirty = m_channelFormName != m_channelEditOriginal.name;
        const bool topicDirty = m_channelFormTopic != m_channelEditOriginal.topic.value_or("");
        const bool nsfwDirty = m_channelFormNsfw != m_channelEditOriginal.nsfw.value_or(false);
        const bool rateDirty = m_channelFormRateLimit != m_channelEditOriginal.rateLimitPerUser.value_or(0);
        m_channelEditOriginal = *fresh;
        if (action.channelName || !nameDirty)
          m_channelFormName = fresh->name;
        if (action.channelTopic || !topicDirty)
          m_channelFormTopic = fresh->topic.value_or("");
        if (action.channelNsfw || !nsfwDirty)
          m_channelFormNsfw = fresh->nsfw.value_or(false);
        if (action.channelRateLimit || !rateDirty)
          m_channelFormRateLimit = fresh->rateLimitPerUser.value_or(0);
      }
      if (action.kind == api::InteractionKind::EditChannelPermission) {
        m_channelPermissionDraft.erase(action.permissionBit);
        m_channelPermissionPending.erase(action.permissionBit);
      }
      if (!m_channelPermissionPending.empty() && action.kind != api::InteractionKind::GrantChannelAccess) {
        if (!(channelPermissionsFor(action.channel).value_or(0) & api::managePermissions)) {
          m_dialogPending = false;
          m_dialogAction.reset();
          m_dialogError = "Your remaining changes were not saved because your permissions changed.";
          m_window.invalidate();
          return true;
        }
        auto next = action;
        next.kind = api::InteractionKind::EditChannelPermission;
        next.revision = ++m_channelFeatureSerial;
        next.channelName.reset();
        next.channelTopic.reset();
        next.channelNsfw.reset();
        next.channelRateLimit.reset();
        next.user = m_channelPermissionTarget;
        next.overwriteType = m_channelPermissionType;
        next.permissionBit = m_channelPermissionPending.begin()->first;
        next.permissionValue = m_channelPermissionPending.begin()->second;
        if (startInteraction(next)) {
          m_dialogAction = next;
          m_dialogPending = true;
        } else {
          m_dialogPending = false;
          m_dialogAction.reset();
          m_dialogError = m_status;
        }
      } else if (action.kind == api::InteractionKind::GrantChannelAccess && !m_channelReturnToPermissions)
        closeDialog();
      else {
        m_dialogPending = false;
        m_dialogAction.reset();
        m_dialogError.clear();
        if (action.kind == api::InteractionKind::GrantChannelAccess) {
          m_dialogKind = "channel-edit";
          m_channelEditPage = "permissions";
          m_channelPermissionTarget = action.user;
          m_channelPermissionType = action.overwriteType;
          m_channelAccessTarget.clear();
          m_channelAccessFilter.clear();
          m_scrolling.stop(m_channelDialogScroll);
          m_channelDialogScroll = 0;
          m_focusedInput.clear();
        }
      }
    }
    m_status.clear();
    m_window.invalidate();
    return true;
  }
  void ClientApplication::jumpToMessage(const std::string& channel, const std::string& message) {
    if (!api::validSnowflake(channel) || !api::validSnowflake(message))
      return;
    if (m_channelId != channel) {
      if (std::ranges::find(m_channels, channel, &api::Channel::id) == m_channels.end()) {
        if (m_threads.find(m_guildId, channel))
          openThread(channel);
        else {
          openThread(channel);
          if (m_openThreadQuery)
            m_pendingMessageJump = std::pair{channel, message};
          return;
        }
      } else
        selectChannel(channel);
    } else
      closeChannelPanel();
    m_unreadTimeline.positioned();
    m_scrolling.stop(m_messageScroll);
    m_timelineAnchor.reset();
    if (std::ranges::find(m_messages, message, &api::Message::id) != m_messages.end())
      m_seekMessage = message;
    else if (!m_demo) {
      if (const auto query = m_history.beginAround(message)) {
        if (m_client.messages(channel, *query)) {
          m_liveChanges.clear();
          m_loadingMessages = true;
          m_seekMessage = message;
        } else
          m_history.fail(*query, "Could not load this message. Try again.");
      }
    }
    m_window.invalidate();
  }
} // namespace app
