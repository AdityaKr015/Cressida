#include "app/client_application.h"
#include "render/scene/text_node.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace app {
  void ClientApplication::prepareDmHomeDemo(const std::string& view) {
    if (view.ends_with("light"))
      m_preferences.theme = "light";
    if (view.ends_with("narrow"))
      m_window.setUiScale(1.7F);
    m_privateChannels = api::parsePrivateChannels(nlohmann::json::parse(R"([
      {"id":"9001","type":1,"recipients":[{"id":"11","username":"maya","global_name":"Maya"}],"last_message_id":"9910"},
      {"id":"9002","type":1,"recipients":[{"id":"12","username":"robin","global_name":"Robin"}],"is_message_request":true,"is_spam":false,"is_message_request_timestamp":"2026-10-18T10:30:00Z"},
      {"id":"9003","type":1,"recipients":[{"id":"13","username":"unknown","global_name":"Unknown sender"}],"is_message_request":true,"is_spam":true}
    ])"));
    m_privateReady = true;
    m_drafts["9001"] = "An unsent message";
    api::UserUpdates people;
    for (const auto& [id, name, type, status] : std::vector<std::tuple<std::string, std::string, int, std::string>>{
             {"11", "Maya", 1, "online"},
             {"14", "Noah", 1, "offline"},
             {"15", "Sam", 3, "offline"},
             {"16", "Rowan", 4, "offline"},
             {"17", "Blocked user", 2, "offline"}
         }) {
      people.users[id].account = api::Account{id, name, {}, name};
      people.users[id].relationship = type;
      people.users[id].status = status;
    }
    m_userDirectory.apply(people);
    openDmPage(view.starts_with("dm-friends") ? "friends" : "requests");
    if (view.starts_with("dm-friends"))
      m_dmFilter = "all";
    if (view.find("selected") != std::string::npos) {
      dmAction("open-request:9002");
      m_dmPreview = api::parseMessages(nlohmann::json::parse(R"([
        {"id":"9991","channel_id":"9002","author":{"id":"12","username":"robin","global_name":"Robin"},"timestamp":"2026-10-18T10:30:00Z","content":"Hello! Do you have a moment? I'd like to ask about your project.\nYou can read this request before accepting it."},
        {"id":"9992","channel_id":"9002","author":{"id":"12","username":"robin","global_name":"Robin"},"timestamp":"2026-10-18T10:31:00Z","content":"The link https://example.com stays inactive here.","attachments":[{"id":"123","filename":"notes.txt","url":"https://cdn.discordapp.com/attachments/9002/123/notes.txt"}]}
      ])"));
      if (view.find("long") != std::string::npos)
        for (int i = 0; i < 30; ++i) {
          auto message = m_dmPreview.front();
          message.id = std::to_string(10000 + i);
          m_dmPreview.push_back(std::move(message));
        }
    }
    if (view == "dm-home-cycle") {
      m_demoOverlay = view;
      m_demoInteractionStep = 0;
    }
  }
  void ClientApplication::demoDmHomeStep() {
    const auto require = [](bool value, const char* message) {
      if (!value)
        throw std::runtime_error(message);
    };
    const auto click = [&](Action action, const std::string& id = std::string{}) {
      const auto found = std::ranges::find_if(m_hits, [&](const auto& hit) {
        return hit.action == action && hit.id == id && hit.height > 0;
      });
      require(found != m_hits.end(), "DM replay requires a real visible action");
      const auto hit = *found;
      const float x = hit.x + hit.width / 2, y = hit.y + hit.height / 2 + m_window.titlebarHeight() - 36;
      m_window.motion(x, y);
      m_window.click(x, y, 1);
      m_window.released();
    };
    const auto preview = [&](const std::string& id, const std::string& text) {
      api::Event event;
      event.operation = api::Operation::Messages;
      event.resource = id;
      event.history = api::HistoryQuery{.request = m_dmPreviewRequest};
      event.messages = {{"9991", id, "Sender", text}};
      require(dmHomeEvent(event), "DM preview consumes only its own history result");
    };
    const auto result = [&](const std::string& error = std::string{}) {
      require(m_dmDemoAction.has_value(), "DM action must be explicitly submitted before acknowledgement");
      api::Event event;
      event.operation = api::Operation::Interaction;
      event.interaction = m_dmDemoAction;
      event.error = error;
      require(dmInteractionResult(event), "DM action acknowledgement is handled independently of message dialogs");
      m_dmDemoAction.reset();
    };
    const auto textCount = [&](const std::string& text) {
      int count = 0;
      const auto inspect = [&](auto&& self, const Node& node) -> void {
        if (node.type() == NodeType::Text && static_cast<const TextNode&>(node).text() == text)
          ++count;
        for (const auto& child : node.children())
          self(self, *child);
      };
      inspect(inspect, *m_scene);
      return count;
    };
    const auto centeredButtons = [&] {
      int checked = 0;
      const auto inspect = [&](auto&& self, const Node& node) -> void {
        if (node.type() == NodeType::Text) {
          const auto& text = static_cast<const TextNode&>(node);
          if (text.text() == "Accept DM"
              || text.text() == "Ignore"
              || text.text() == "Message"
              || text.text() == "Remove") {
            float x = text.x(), y = text.y();
            for (auto* ancestor = text.parent(); ancestor && ancestor != m_scene.get(); ancestor = ancestor->parent()) {
              x += ancestor->x();
              y += ancestor->y();
            }
            const auto metrics = m_window.target().renderer().measureText(
                text.text(), text.fontSize(), FontWeight::Medium, text.maxWidth(), 1
            );
            const float centerX = x + metrics.width / 2, centerY = y + (metrics.top + metrics.bottom) / 2;
            require(
                std::ranges::any_of(
                    m_hits,
                    [&](const auto& hit) {
                      return hit.action == Action::DmAction
                          && std::abs(centerX - hit.x - hit.width / 2) < .6F
                          && std::abs(centerY - hit.y - hit.height / 2) < .6F;
                    }
                ),
                "DM caption glyph bounds must be horizontally and vertically centered in their actual button hit area"
            );
            ++checked;
          }
        }
        for (const auto& child : node.children())
          self(self, *child);
      };
      inspect(inspect, *m_scene);
      require(checked >= 2, "The DM replay must inspect rendered button captions");
    };
    switch (m_demoInteractionStep++) {
    case 0:
      require(
          messageRequestCount() == 1 && m_channels.size() == 1 && m_channelId.empty(),
          "Requests stay out of ordinary DMs and never open a readable timeline"
      );
      require(
          textCount("1") >= 2 && textCount("Spam (1)") == 1,
          "Both navigation badges exclude Spam, whose count appears only on its own tab"
      );
      click(Action::DmAction, "open-request:9002");
      preview("9002", "Hello! Here is a link https://example.com and a file.");
      for (int i = 0; i < 17; ++i)
        m_dmPreview.push_back({std::to_string(10000 + i), "9002", "Robin", "Another message in the pending request."});
      m_dmHomeScroll = 10000;
      break;
    case 1:
      require(
          m_dmPreview.size() == 18 && m_dmPreviewBounds.width > 0 && m_channelId.empty(),
          "Clicking a request opens an independent conversation panel"
      );
      require(textCount("Preview") == 0 && textCount("Close") == 0, "Request rows have no separate preview button");
      centeredButtons();
      m_window.motion(m_dmPreviewBounds.x + 30, m_dmPreviewBounds.y + 200 + m_window.titlebarHeight() - 36);
      m_window.scroll(70);
      require(
          m_scrolling.active(m_dmPreviewScroll) && !m_scrolling.active(m_dmHomeScroll),
          "Request history scrolls independently of the inbox"
      );
      click(Action::DmAction, "close-request");
      require(
          m_dmPreviewId.empty() && m_dmPreview.empty(),
          "Closing the conversation panel preserves the inbox without retaining its messages"
      );
      break;
    case 2: {
      click(Action::DmAction, "open-request:9002");
      api::Event stale;
      stale.operation = api::Operation::Messages;
      stale.resource = "9002";
      stale.history = api::HistoryQuery{.request = m_dmPreviewRequest - 1};
      stale.messages = {{"9991", "9002", "Robin", "Stale preview"}};
      require(dmHomeEvent(stale) && m_dmPreview.empty(), "Reopening the same request rejects an older panel response");
      preview("9002", "Hello again!");
      break;
    }
    case 3:
      require(m_dmHomeScroll == 0, "Request geometry is clamped before painting rows and placing their controls");
      require(
          m_dmPreview.size() == 1 && m_channelId.empty(),
          "Preview preserves request state without a composer or read acknowledgement"
      );
      require(
          std::ranges::none_of(m_hits, [](const auto& hit) { return hit.action == Action::OpenUrl; }),
          "Request links are plain inactive text"
      );
      {
        const auto serial = m_dmPreviewRequest;
        click(Action::DmAction, "open-request:9002");
        require(
            m_dmPreviewRequest == serial && m_dmPreview.size() == 1,
            "Clicking the selected request keeps its panel and history intact"
        );
      }
      click(Action::DmAction, "accept:9002");
      require(messageRequestCount() == 1, "Accept waits for a successful server acknowledgement");
      result("Synthetic request failure");
      require(
          messageRequestCount() == 1 && !m_dmStatus.empty(),
          "Failed acceptance retains the request and visible feedback"
      );
      break;
    case 4:
      click(Action::DmAction, "accept:9002");
      result();
      require(
          messageRequestCount() == 0 && m_channelId == "9002" && m_dmPage.empty(),
          "Accepted requests become ordinary conversations and open only after success"
      );
      break;
    case 5:
      click(Action::Home);
      require(m_dmPage == "friends" && m_channelId.empty(), "Home opens the native Friends view");
      break;
    case 6:
      click(Action::DmPage, "requests");
      break;
    case 7:
      click(Action::DmAction, "filter:spam");
      break;
    case 8:
      click(Action::DmAction, "open-request:9003");
      preview("9002", "Stale response");
      require(m_dmPreview.empty(), "A previous request cannot populate a newly opened preview");
      preview("9003", "Unwanted message");
      break;
    case 9:
      click(Action::DmAction, "ignore:9003");
      require(
          messageRequestCount() == 0 && m_dmPending.empty(),
          "Ignoring asks for confirmation before changing account state"
      );
      break;
    case 10:
      click(Action::DmAction, "confirm-ignore:9003");
      require(m_dmPending.contains("9003") && messageRequestCount() == 0, "Ignoring stays pending until acknowledged");
      result();
      require(messageRequestCount() == 0 && m_dmPreview.empty(), "Ignored requests clear their badge and preview");
      break;
    case 11:
      require(
          textCount("Spam (1)") == 0 && textCount("Spam") == 1, "The Spam tab count clears after an acknowledged ignore"
      );
      click(Action::DmPage, "friends");
      break;
    case 12:
      click(Action::DmAction, "filter:pending");
      break;
    case 13:
      click(Action::DmAction, "friend-accept:15");
      result();
      require(m_userDirectory.relationship("15") == 1, "Pending friend requests use the functional accept action");
      break;
    case 14:
      click(Action::DmAction, "filter:all");
      break;
    case 15:
      centeredButtons();
      focusControl("dm-search");
      pasteControl("dm-search", "Maya");
      require(m_dmSearch == "Maya" && !m_profileDirty, "Native conversation search never dirties profile settings");
      break;
    case 16:
      require(
          std::ranges::none_of(
              m_hits, [](const auto& hit) { return hit.action == Action::DmAction && hit.id == "chat:14"; }
          ),
          "Friend search filters unmatched people"
      );
      click(Action::DmAction, "chat:11");
      break;
    case 17:
      require(
          m_channelId == "9001" && m_drafts["9001"] == "An unsent message",
          "Opening an existing friend DM preserves its composer draft"
      );
      require(
          messageRequestCount() == 0 && m_dmPending.empty() && m_dmPreview.empty(),
          "Finished DM actions leave no stale request data"
      );
      std::clog << "Offline DM navigation, requests, previews, badges and friends replay passed\n";
      break;
    }
    m_window.invalidate();
  }
} // namespace app
