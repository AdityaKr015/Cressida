#include "app/client_application.h"
#include "app/theme.h"
#include "render/scene/text_node.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace app {
  namespace {
    constexpr auto minimumExpiryDelay = std::chrono::milliseconds{1};
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
          require(
              y < m_draftY + m_window.titlebarHeight() - layout::Window::titlebarHeight,
              "Typing text stays above the native composer"
          );
        }
        for (const auto& child : node.children())
          self(self, *child);
      };
      inspect(inspect, *m_scene);
      require(found && m_typingVisible, "The visible indicator paints the actual typing summary");
    };
    const auto step = m_demoInteractionStep++;
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
    } else if (step == 11)
      require(!m_typingVisible && typingText().empty(), "Cleared typing indicators leave no animation or stale text");
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
  void ClientApplication::drawTyping(Node& root, float x, float y, float width, const std::string& text) {
    m_typingVisible = !settingsVisible() && m_dialogKind.empty() && m_viewImage.empty();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(TypingIndicator::Clock::now().time_since_epoch());
    const float phase =
        static_cast<float>(elapsed.count() % layout::Typing::cycle.count()) / layout::Typing::cycle.count();
    for (std::size_t i = 0; i < layout::Typing::dotCount; ++i) {
      auto color = theme::muted;
      if (!m_preferences.reducedMotion) {
        const float wave =
            (1 + std::sin(2 * std::numbers::pi_v<float> * (phase - static_cast<float>(i) / layout::Typing::dotCount)))
            / 2;
        color.a *= Style::disabledOutlineAlpha + (1 - Style::disabledOutlineAlpha) * wave;
      }
      rectangle(
          root, x + i * layout::Typing::dotStep, y + (layout::Typing::height - layout::Typing::dotSize) / 2,
          layout::Typing::dotSize, layout::Typing::dotSize, color, layout::Typing::dotSize / 2
      );
    }
    const auto metrics = m_window.target().renderer().measureText(text, Style::fontSizeDescription);
    label(
        root, x + layout::Typing::textInset, y + (layout::Typing::height - (metrics.bottom - metrics.top)) / 2,
        std::max(0.0F, width - layout::Typing::textInset), text, Style::fontSizeDescription, theme::muted
    );
  }
} // namespace app
