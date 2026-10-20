#include "app/client_application.h"
#include "app/theme.h"
#include "render/scene/rect_node.h"
#include "render/scene/text_node.h"
#include "ui/controls/slider.h"

#include <algorithm>
#include <cmath>
#include <linux/input-event-codes.h>
#include <numbers>
#include <stdexcept>

namespace app {
  namespace {
    constexpr auto minimumExpiryDelay = std::chrono::milliseconds{1};
  }
  void ClientApplication::demoFloatingStep() {
    const auto require = [](bool value, const char* message) {
      if (!value)
        throw std::runtime_error(message);
    };
    const auto surface = [&](const layout::Frame& frame) {
      const auto found = std::ranges::find_if(m_scene->children(), [&](const auto& node) {
        return node->type() == NodeType::Rect
            && node->x() == frame.x
            && node->y() == frame.y
            && node->width() == frame.width
            && node->height() == frame.height;
      });
      require(found != m_scene->children().end(), "Floating panes paint their measured layout frames");
      for (const auto& node : m_scene->children())
        if (node->type() == NodeType::Rect
            && node->x() == frame.x
            && node->y() == frame.y
            && node->width() == frame.width
            && node->height() == frame.height)
          require(!node->hitTestVisible(), "Pane backgrounds, masks and borders never cover native input targets");
      const auto& style = static_cast<const RectNode&>(**found).style();
      if (frame.x > Style::spaceSm)
        require(
            style.fill == theme::background, "Channel, member, header, chat and input panels share the chat surface"
        );
      require(
          style.radius == Radii{Style::scaledRadiusXl()} && style.borderWidth == Style::borderWidth,
          "Every floating pane follows the shared corner scale and border token"
      );
    };
    const auto panes = [&] {
      require(
          m_floatingFrames.size() == 7,
          "The rail, channels, profile, header, conversation, composer and members float independently"
      );
      for (const auto& frame : m_floatingFrames) {
        require(frame.width > 0 && frame.height > 0, "Floating panes retain usable dimensions");
        surface(frame);
      }
      for (const auto& hit : m_hits)
        if (hit.x < layout::Sidebar::railWidth
            && (hit.action == Action::Home
                || hit.action == Action::Guild
                || hit.action == Action::Folder
                || hit.action == Action::DirectMessage))
          require(
              hit.x + hit.width / 2 == layout::Sidebar::railWidth / 2
                  && hit.x >= Style::spaceSm + Style::spaceCompact
                  && hit.x + hit.width <= layout::Sidebar::railWidth - Style::spaceSm - Style::spaceCompact,
              "Rail icons and folders are centered with breathing room on both sides"
          );
      const float h = m_window.height() + layout::Window::titlebarHeight - m_window.titlebarHeight();
      const auto account = layout::accountFrame(h, chatLeft());
      surface({account.x, account.y, account.width, account.height});
      surface(
          layout::floatingFrame(
              0, layout::Window::titlebarHeight, layout::Sidebar::railWidth,
              account.y - Style::spaceSm - layout::Window::titlebarHeight
          )
      );
      surface(
          layout::floatingFrame(
              layout::Sidebar::railWidth, layout::Window::titlebarHeight, channelSidebarWidth(),
              account.y - Style::spaceSm - layout::Window::titlebarHeight
          )
      );
      surface(
          layout::floatingFrame(
              chatLeft(), layout::Window::titlebarHeight, m_window.width() - chatLeft(), layout::ChannelHeader::height
          )
      );
      const auto caption = std::ranges::find_if(m_scene->children(), [&](const auto& node) {
        return node->type() == NodeType::Text
            && static_cast<const TextNode&>(*node).text() == channelName(*selectedChannel())
            && node->x() > chatLeft()
            && node->y() < layout::Window::titlebarHeight + layout::ChannelHeader::height;
      });
      require(caption != m_scene->children().end(), "The actual channel header caption is mounted");
      const auto& text = static_cast<const TextNode&>(**caption);
      const auto metrics = m_window.target().renderer().measureText(
          text.text(), text.fontSize(), text.fontWeight(), text.maxWidth(), text.maxLines()
      );
      require(
          std::abs(text.y() + (metrics.top + metrics.bottom) / 2 - layout::ChannelHeader::centerY) < Style::borderWidth,
          "Channel names center their actual glyph bounds in the floating header, including emoji and ellipsis"
      );
      const auto composer = layout::composerFrame(h, layout::Composer::minimumHeight);
      const auto unreadChannel = std::ranges::find_if(m_hits, [](const auto& hit) {
        return hit.action == Action::Channel && hit.id == "103";
      });
      require(unreadChannel != m_hits.end(), "The floating fixture includes an unread channel");
      const auto unreadMarker = std::ranges::find_if(m_scene->children(), [&](const auto& node) {
        return node->type() == NodeType::Rect
            && node->x() == layout::Sidebar::unreadX
            && node->y() == unreadChannel->y + (layout::Sidebar::rowHeight - layout::Sidebar::unreadHeight) / 2
            && node->width() == layout::Sidebar::unreadWidth
            && node->height() == layout::Sidebar::unreadHeight;
      });
      require(
          unreadMarker != m_scene->children().end()
              && (*unreadMarker)->x() > layout::Sidebar::railWidth + Style::spaceSm
              && (*unreadMarker)->x() + (*unreadMarker)->width()
                  < layout::Sidebar::railWidth + layout::Sidebar::rowInset * 2,
          "Unread markers stay inside the channel panel and clear of channel icons"
      );
      const float memberX = m_window.width() - m_floatingFrames.back().width - Style::spaceSm * 2;
      const auto memberHit = std::ranges::find_if(m_hits, [&](const auto& hit) {
        return hit.action == Action::UserProfile && hit.x == memberX + Style::spaceMd;
      });
      require(
          memberHit != m_hits.end() && memberHit->y == layout::Members::top + layout::Members::groupHeight,
          "The first member follows the compact group heading with no redundant section header"
      );
      const auto noHeading = [&](auto&& self, const Node& node) -> void {
        if (node.type() == NodeType::Text) {
          const auto& text = static_cast<const TextNode&>(node).text();
          require(text != "MEMBERS" && text != "PARTICIPANTS", "The member list has no redundant title row");
        }
        for (const auto& child : node.children())
          self(self, *child);
      };
      noHeading(noHeading, *m_scene);
      const auto& conversation = m_floatingFrames[4];
      const auto scrollbar = m_widgets.find("chat-scrollbar");
      require(
          scrollbar != m_widgets.end()
              && scrollbar->second->x() >= conversation.x
              && scrollbar->second->x() + scrollbar->second->width() <= conversation.x + conversation.width
              && scrollbar->second->y() + scrollbar->second->height() <= composer.typingTop,
          "The timeline scrollbar stays inside the conversation panel above the typing row"
      );
      require(
          conversation.y + conversation.height == composer.panelBottom
              && composer.panelBottom == account.y - Style::spaceSm * 2
              && composer.typingTop >= m_messageViewportBottom
              && composer.typingTop + layout::Typing::height < composer.panelBottom,
          "Typing stays inside the conversation panel, whose bottom aligns with both sidebars"
      );
      require(m_composerTop == account.y, "Floating profile and default input share their height and bottom margin");
    };
    const auto radius = [&](bool maximum) {
      const auto found = m_widgets.find("user-account-slider:local:corner_radius");
      require(
          found != m_widgets.end() && found->second->parent(), "Appearance exposes a retained native radius slider"
      );
      auto& control = *static_cast<Slider*>(found->second);
      float x = 0, y = 0;
      Node::mapToScene(
          &control, maximum ? control.width() - Style::spaceXxs : Style::spaceXxs, control.height() / 2, x, y
      );
      m_window.motion(x, y);
      m_window.click(x, y, BTN_LEFT);
      m_window.released();
      require(
          m_preferences.cornerRadius == (maximum ? Style::cornerRadiusMaximumPercent : 0),
          "Dragging the native corner slider applies its full bounded range"
      );
      require(!m_profileDirty, "Corner changes do not dirty account profile fields");
    };
    const auto step = m_demoInteractionStep++;
    if (step == 0) {
      panes();
      openSettings();
      selectSettingsPage("appearance");
    } else if (step == 1) {
      radius(false);
    } else if (step == 2) {
      require(Style::cornerRadiusScale() == 0, "Square corners apply before settings are measured and painted");
      activate({0, 0, 0, 0, Action::CloseSettings, {}});
    } else if (step == 3) {
      panes();
      for (auto& channel : m_channels)
        if (channel.id == m_channelId)
          channel.name = "offtopic";
      openSettings();
      selectSettingsPage("appearance");
    } else if (step == 4) {
      radius(true);
    } else if (step == 5) {
      require(Style::cornerRadiusScale() == 2, "Maximum rounding applies immediately to retained settings controls");
      activate({0, 0, 0, 0, Action::CloseSettings, {}});
    } else if (step == 6) {
      panes();
      for (auto& channel : m_channels)
        if (channel.id == m_channelId)
          channel.name = "☕-a-long-channel-name-that-needs-to-be-ellipsized-in-the-header";
      extendedPreference("corner_radius:" + std::to_string(Style::cornerRadiusDefaultPercent));
    } else if (step == 7) {
      panes();
      require(Style::cornerRadiusScale() == 1, "Default rounding can be restored without changing layout geometry");
    }
    m_window.invalidate();
  }
  void ClientApplication::demoTypingStep() {
    const auto require = [](bool value, const char* message) {
      if (!value)
        throw std::runtime_error(message);
    };
    const auto send = [&](const std::string& user, const std::string& name = "") {
      const auto timestamp =
          std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      const auto parsed = api::parseTypingStart(
          {{"channel_id", m_channelId},
           {"user_id", user},
           {"timestamp", timestamp},
           {"guild_id", m_dmMode ? "" : m_guildId}}
      );
      require(parsed.has_value(), "The offline replay uses a parsed Gateway typing payload");
      api::Event event{.operation = api::Operation::TypingStart, .session = m_session, .resource = m_channelId};
      event.typing = *parsed;
      event.typing->name = name;
      typingEvent(event);
    };
    const auto rendered = [&](const std::string& expected) {
      bool found = false;
      const auto inspect = [&](auto&& self, const Node& node) -> void {
        if (node.type() == NodeType::Text && static_cast<const TextNode&>(node).text() == expected) {
          found = true;
          float x = 0, y = 0;
          Node::mapToScene(&node, 0, 0, x, y);
          const float chrome = m_window.titlebarHeight() - layout::Window::titlebarHeight;
          const float sceneHeight = m_window.height() - chrome;
          const auto composer = layout::composerFrame(sceneHeight, layout::Composer::minimumHeight);
          const auto metrics = m_window.target().renderer().measureText(expected, Style::fontSizeDescription);
          require(
              y + metrics.top >= composer.typingTop + chrome
                  && y + metrics.bottom <= composer.typingTop + layout::Typing::height + chrome
                  && y + metrics.bottom < composer.panelBottom + chrome,
              "Typing text stays inside its reserved row within the conversation panel"
          );
        }
        for (const auto& child : node.children())
          self(self, *child);
      };
      inspect(inspect, *m_scene);
      require(found && m_typingVisible, "The visible indicator paints the actual typing summary");
    };
    const auto badges = [&](std::size_t expected) {
      std::size_t count = 0;
      const auto inspect = [&](auto&& self, const Node& node) -> void {
        if (node.type() == NodeType::Base
            && node.width() == layout::TypingBadge::width
            && node.height() == layout::TypingBadge::height
            && node.children().size() == layout::Typing::dotCount + 2) {
          ++count;
          for (std::size_t i = 0; i < layout::Typing::dotCount; ++i) {
            const auto& dot = *node.children()[i + 2];
            require(
                dot.type() == NodeType::Rect
                    && dot.width() == layout::TypingBadge::dotSize
                    && dot.height() == layout::TypingBadge::dotSize
                    && dot.x() == (node.width() - layout::TypingBadge::dotsWidth) / 2 + i * layout::TypingBadge::dotStep
                    && dot.y() == (node.height() - layout::TypingBadge::dotSize) / 2,
                "Avatar typing dots remain centered inside the measured status pill"
            );
            if (m_preferences.reducedMotion)
              require(
                  static_cast<const RectNode&>(dot).style().fill.a == 1,
                  "Reduced motion renders static avatar typing dots"
              );
          }
        }
        for (const auto& child : node.children())
          self(self, *child);
      };
      inspect(inspect, *m_scene);
      if (count != expected)
        throw std::runtime_error(
            "Avatar typing replay frame "
            + std::to_string(m_demoInteractionStep - 1)
            + " expected "
            + std::to_string(expected)
            + " typing pills, found "
            + std::to_string(count)
        );
    };
    const auto step = m_demoInteractionStep++;
    const float sceneHeight = m_window.height() + layout::Window::titlebarHeight - m_window.titlebarHeight();
    require(
        m_composerTop == sceneHeight - layout::Sidebar::accountReserve,
        "The default input bar aligns with the profile area above their shared bottom margin"
    );
    const auto accountHit = std::ranges::find(m_hits, Action::TogglePresence, &Hit::action);
    const auto account = layout::accountFrame(sceneHeight, chatLeft());
    require(
        accountHit != m_hits.end()
            && accountHit->x == account.x
            && accountHit->y == m_composerTop
            && accountHit->height == layout::Composer::minimumHeight,
        "The profile area and its hit region match the default input bar's height and position"
    );
    const auto accountSurface = std::ranges::find_if(m_scene->children(), [&](const auto& node) {
      return node->type() == NodeType::Rect
          && node->x() == account.x
          && node->y() == account.y
          && node->width() == account.width
          && node->height() == account.height;
    });
    require(
        accountSurface != m_scene->children().end()
            && static_cast<const RectNode&>(**accountSurface).style().radius == Radii{Style::scaledRadiusXl()}
            && account.x > 0
            && account.x + account.width < chatLeft(),
        "The profile paints as a floating rounded card with side margins"
    );
    for (const auto& hit : m_hits)
      if (hit.action == Action::Guild
          || hit.action == Action::Folder
          || hit.action == Action::DirectMessage
          || hit.action == Action::Channel)
        require(
            hit.y + hit.height <= account.y - Style::spaceSm,
            "Server, DM and channel hit regions remain above the floating account card"
        );
    require(
        m_composerTop + layout::Composer::minimumHeight == sceneHeight - layout::Composer::bottomInset,
        "The composer stays at the shared bottom inset whether or not people are typing"
    );
    const TypingLayoutSnapshot current{m_channelId,          m_composerTop,
                                       m_messageViewportTop, m_messageViewportBottom,
                                       m_messageMaxScroll,   m_messageScroll,
                                       m_messageTops};
    if (m_demoTypingLayout && m_demoTypingLayout->channel == current.channel)
      require(
          current.composerTop == m_demoTypingLayout->composerTop
              && current.viewportTop == m_demoTypingLayout->viewportTop
              && current.viewportBottom == m_demoTypingLayout->viewportBottom
              && current.maximumScroll == m_demoTypingLayout->maximumScroll
              && current.scroll == m_demoTypingLayout->scroll
              && current.messageTops == m_demoTypingLayout->messageTops,
          "Typing starts, expiry, blocking and completion never move the timeline, scroll range or composer"
      );
    m_demoTypingLayout = current;
    const std::size_t expectedBadges[] = {0, 1, 2, 4, 3, 1, 2, 0, 0, 0, 0, 0, 3, 0, 3, 0};
    require(step < typingReplayFrames, "Avatar typing replay remains within its frame count");
    badges(expectedBadges[step]);
    if (step == 0) {
      send("7", "Maya");
      require(
          typingText() == "Maya is typing…" && m_typingTimer.active(),
          "A Gateway typing event schedules one expiry timer"
      );
      send(m_account.id, "Self");
      api::Event wrong{.operation = api::Operation::TypingStart, .session = m_session + 1, .resource = m_channelId};
      wrong.typing = api::TypingStart{m_channelId, m_guildId, "8", "Stale", 0};
      typingEvent(wrong);
      require(typingText() == "Maya is typing…", "Own typing and stale-account events stay hidden");
    } else if (step == 1) {
      rendered("Maya is typing…");
      send("8", "Noah");
      m_userDirectory.consume("RELATIONSHIP_ADD", {{"id", "9"}, {"type", 2}});
      send("9", "Blocked");
      require(typingText() == "Maya and Noah are typing…", "Blocked users stay out of typing names and counts");
    } else if (step == 2) {
      rendered("Maya and Noah are typing…");
      send("10", "Alex");
      send("11", "Dani");
    } else if (step == 3) {
      rendered("Maya, Noah, Alex, and others are typing…");
      api::Event sent{.operation = api::Operation::MessageCreate, .session = m_session, .resource = m_channelId};
      sent.messages.push_back({});
      sent.messages.front().authorId = "8";
      typingEvent(sent);
      require(typingText() == "Maya, Alex, and Dani are typing…", "Sending a message immediately removes that person");
    } else if (step == 4) {
      rendered("Maya, Alex, and Dani are typing…");
      api::Channel dm{"9011", "Group conversation", 3};
      dm.recipients = {{"7", "DM friend"}, {"8", "Noah"}};
      m_privateChannels = {dm, {"9012", "Another conversation", 1}};
      showDirectMessages();
      selectChannel("9011");
      updateDmMembers();
      require(typingText().empty() && !m_typingTimer.active(), "DM navigation clears preceding guild typing state");
      send("7");
    } else if (step == 5) {
      rendered("DM friend is typing…");
      send("8", "Noah");
      send("10", "Alex");
      send("11", "Dani");
      m_preferences.reducedMotion = true;
    } else if (step == 6) {
      rendered("DM friend, Noah, Alex, and others are typing…");
      m_typing.expire(TypingIndicator::Clock::now() + TypingIndicator::lifetime);
      armTypingExpiry();
      require(
          typingText().empty() && !m_typingTimer.active(), "Expiry removes names and stops the timer without polling"
      );
    } else if (step == 7) {
      require(!m_typingVisible, "An empty indicator reserves no visible typing row");
      send("7");
      selectChannel("9012");
      updateDmMembers();
      require(typingText().empty() && !m_typingTimer.active(), "Switching chats clears transient typing state");
    } else if (step == 8) {
      send("7", "<b>Literal name</b>");
    } else if (step == 9) {
      rendered("<b>Literal name</b> is typing…");
      api::Event disconnected{.operation = api::Operation::Gateway, .session = m_session};
      typingEvent(disconnected);
      require(typingText().empty() && !m_typingTimer.active(), "Disconnects clear typing indicators immediately");
    } else if (step == 10) {
      send("7", "Maya");
      api::Event unrelated{.operation = api::Operation::MessageCreate, .session = m_session, .resource = "999"};
      unrelated.messages.push_back({});
      unrelated.messages.front().authorId = "7";
      typingEvent(unrelated);
      require(typingText() == "Maya is typing…", "Messages in another chat do not clear the current indicator");
      clearTyping();
    } else if (step == 11) {
      require(!m_typingVisible && typingText().empty(), "Cleared typing indicators leave no animation or stale text");
      for (auto* channels : {&m_privateChannels, &m_channels}) {
        const auto dm = std::ranges::find(*channels, m_channelId, &api::Channel::id);
        require(dm != channels->end(), "The direct-message avatar fixture is selected");
        dm->recipients = {{"7", "Maya"}};
      }
      updateDmMembers();
      m_preferences.reducedMotion = false;
      send("7", "Maya");
      require(
          userTyping("7", m_channelId) && !userTyping("7", "9011"),
          "A DM typist does not leak into another conversation with the same person"
      );
    } else if (step == 12) {
      rendered("Maya is typing…");
      m_userDirectory.consume("RELATIONSHIP_ADD", {{"id", "7"}, {"type", 2}});
      require(
          !userTyping("7", m_channelId) && typingText().empty(),
          "Blocking an active typist suppresses avatar and composer indicators together"
      );
    } else if (step == 13) {
      m_userDirectory.consume("RELATIONSHIP_REMOVE", {{"id", "7"}});
      clearTyping();
      send("7", "Maya");
      m_preferences.reducedMotion = true;
    } else if (step == 14) {
      rendered("Maya is typing…");
      api::Event sent{.operation = api::Operation::MessageCreate, .session = m_session, .resource = m_channelId};
      sent.messages.push_back({});
      sent.messages.front().authorId = "7";
      typingEvent(sent);
    } else if (step == 15) {
      require(
          !m_typingVisible && typingText().empty() && !m_typingTimer.active(),
          "Message completion restores avatar presence without a typing animation or timer"
      );
      Node probe;
      statusBadge(probe, 0, 0, "dnd", theme::sidebar, "7", m_channelId);
      require(
          probe.children().size() == 3
              && static_cast<const RectNode&>(*probe.children()[1]).style().fill == theme::presenceColor("dnd"),
          "The normal presence glyph is restored after typing without changing its status"
      );
    }
    m_window.invalidate();
  }
  void ClientApplication::clearTyping() {
    m_typingTimer.stop();
    m_typing.clear();
    m_typingVisible = false;
  }
  void ClientApplication::armTypingExpiry() {
    m_typingTimer.stop();
    if (const auto deadline = m_typing.deadline()) {
      const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(*deadline - TypingIndicator::Clock::now());
      m_typingTimer.start(std::max(minimumExpiryDelay, remaining), [this] {
        m_typing.expire();
        armTypingExpiry();
        m_window.invalidate();
      });
    }
  }
  void ClientApplication::typingEvent(const api::Event& event) {
    if (event.session != m_session)
      return;
    if (event.operation == api::Operation::Gateway && !event.live) {
      clearTyping();
      return;
    }
    if (event.operation == api::Operation::MessageCreate) {
      bool changed = false;
      for (const auto& message : event.messages)
        changed |= m_typing.message(event.resource, message.authorId);
      if (changed) {
        armTypingExpiry();
        m_window.invalidate();
      }
      return;
    }
    if (event.operation != api::Operation::TypingStart
        || !event.typing
        || !event.error.empty()
        || !m_connected
        || (!m_live && !m_demo)
        || m_deletedChannels.contains(event.resource))
      return;
    auto typing = *event.typing;
    if (event.resource != typing.channel
        || typing.channel != m_channelId
        || typing.guild != (m_dmMode ? "" : m_guildId)
        || typing.user == m_account.id
        || m_userDirectory.relationship(typing.user) == 2
        || m_userDirectory.ignored(typing.user))
      return;
    if (typing.name.empty()) {
      const auto member = std::ranges::find(m_members, typing.user, [](const auto& row) { return row.user.id; });
      typing.name = member != m_members.end() ? member->user.name : lookupUser(typing.user).name;
      if (typing.name == "Unknown user")
        typing.name.clear();
    }
    if (m_typing.start(typing, m_channelId, m_dmMode ? "" : m_guildId, m_account.id)) {
      armTypingExpiry();
      m_window.invalidate();
    }
  }
  std::string ClientApplication::typingText() const {
    if (m_channelId.empty() || m_deletedChannels.contains(m_channelId))
      return {};
    return m_typing.text([&](const auto& user) -> std::optional<std::string> {
      if (m_userDirectory.relationship(user.id) == 2 || m_userDirectory.ignored(user.id))
        return {};
      return userName(user.id, user.name);
    });
  }
  bool ClientApplication::userTyping(const std::string& user, const std::string& channel) const {
    return !channel.empty()
        && channel == m_channelId
        && !m_deletedChannels.contains(channel)
        && user != m_account.id
        && m_userDirectory.relationship(user) != 2
        && !m_userDirectory.ignored(user)
        && m_typing.contains(channel, user);
  }
  void ClientApplication::drawTypingDots(Node& parent, float x, float y, float size, float step, Color baseColor) {
    m_typingVisible |= !settingsVisible() && m_dialogKind.empty() && m_viewImage.empty();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(TypingIndicator::Clock::now().time_since_epoch());
    const float phase =
        static_cast<float>(elapsed.count() % layout::Typing::cycle.count()) / layout::Typing::cycle.count();
    for (std::size_t i = 0; i < layout::Typing::dotCount; ++i) {
      auto color = baseColor;
      if (!m_preferences.reducedMotion) {
        const float wave =
            (1 + std::sin(2 * std::numbers::pi_v<float> * (phase - static_cast<float>(i) / layout::Typing::dotCount)))
            / 2;
        color.a *= Style::disabledOutlineAlpha + (1 - Style::disabledOutlineAlpha) * wave;
      }
      rectangle(parent, x + i * step, y, size, size, color, size / 2);
    }
  }
  void ClientApplication::drawTyping(Node& root, float x, float y, float width, const std::string& text) {
    drawTypingDots(
        root, x, y + (layout::Typing::height - layout::Typing::dotSize) / 2, layout::Typing::dotSize,
        layout::Typing::dotStep, theme::muted
    );
    const auto metrics = m_window.target().renderer().measureText(text, Style::fontSizeDescription);
    label(
        root, x + layout::Typing::textInset, y + (layout::Typing::height - (metrics.bottom - metrics.top)) / 2,
        std::max(0.0F, width - layout::Typing::textInset), text, Style::fontSizeDescription, theme::muted
    );
  }
} // namespace app
