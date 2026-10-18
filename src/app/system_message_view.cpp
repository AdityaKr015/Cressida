#include "app/client_application.h"
#include "app/system_message.h"
#include "app/theme.h"
#include "render/scene/text_node.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace app {
  void ClientApplication::drawJoinNotice(
      Node& parent, const api::Message& message, const MessageLayout& layout, float y, float width
  ) {
    using namespace theme;
    const float absoluteX = chatLeft() + 74, absoluteY = m_messageViewportTop + y;
    const bool over = hovered(chatLeft() + 24, absoluteY, width + 50, layout.height);
    glyph(parent, 8, y + 2, 20, "arrow-right", presenceColor("online"));
    m_messageText[message.id] = layout.plainText;
    m_hits.push_back({absoluteX, absoluteY, width, layout.height, Action::SelectMessage, message.id});
    for (const auto& run : layout.runs) {
      const float x = 50 + run.x, top = y + run.y;
      if (m_selectedMessage == message.id && m_selectedText == layout.plainText && m_chatEdit.selected()) {
        const auto from = std::max(std::min(m_chatEdit.caret, m_chatEdit.anchor), run.offset);
        const auto to = std::min(std::max(m_chatEdit.caret, m_chatEdit.anchor), run.offset + run.text.size());
        if (to > from) {
          const auto measure = [&](std::string_view text) {
            return m_window.target()
                .renderer()
                .measureText(
                    messageMarkup(text, run.style), run.style.size, FontWeight::Normal, 0, 1, TextAlign::Start, {},
                    TextEllipsize::End, true
                )
                .width;
          };
          const float offset = run.image.empty() ? measure(std::string_view(run.text).substr(0, from - run.offset)) : 0;
          const float length =
              run.image.empty() ? measure(std::string_view(run.text).substr(from - run.offset, to - from)) : run.width;
          rectangle(parent, x + offset, top, length, run.height, rgbHex(0x574D70), 2);
        }
      }
      if (!run.image.empty()) {
        if (!emojiImage(parent, x, top, run.width, run.image))
          label(parent, x, top, run.width, "…", run.style.size, foreground);
      } else if (run.style.mention)
        roleLabel(
            parent, x, top, run.width + 2, run.text, run.style.size, foreground, FontWeight::Normal, message.authorId,
            "join:" + message.id, over, message.memberRoles
        );
      else
        label(
            parent, x, top, run.width + 2, messageMarkup(run.text, run.style), run.style.size, muted,
            FontWeight::Normal, 1, true
        );
      m_textAreas.push_back({run, message.id, absoluteX + run.x, absoluteY + run.y});
      if (run.style.mention && !message.authorId.empty()) {
        Hit hit{absoluteX + run.x, absoluteY + run.y, run.width, run.height, Action::UserProfile, message.authorId};
        const float bottom = std::min(m_messageViewportBottom, hit.y + hit.height);
        hit.y = std::max(m_messageViewportTop, hit.y);
        hit.height = std::max(0.0F, bottom - hit.y);
        m_hits.push_back(hit);
        hit.action = Action::UserMenu;
        m_contextTargets.push_back(std::move(hit));
      }
    }
    if (!layout.runs.empty() && message.timestamp.size() >= 16) {
      const auto& last = layout.runs.back();
      label(
          parent, 50 + last.x + last.width + 6, y + last.y + 4, message.deleted ? 120 : 36,
          message.timestamp.substr(11, 5) + (message.deleted ? " · Deleted" : ""), 10,
          message.deleted ? rgbHex(m_preferences.theme == "light" ? 0xB52C39 : 0xEF7D86) : muted
      );
    }
    reactionStrip(&parent, message, 50, y + std::max(22.0F, layout.height) + 6, width, true);
  }

  void ClientApplication::demoJoinStep() {
    const auto require = [](bool ok, const char* reason) {
      if (!ok)
        throw std::runtime_error(reason);
    };
    const auto step = m_demoInteractionStep++;
    int joinArrows = 0;
    const auto inspect = [&](auto&& self, const Node& node) -> void {
      if (node.type() == NodeType::Text) {
        const auto& text = static_cast<const TextNode&>(node);
        require(text.text() != "[System message]", "Join notices must not show the generic placeholder");
      }
      for (const auto& child : node.children())
        self(self, *child);
    };
    inspect(inspect, *m_scene);
    for (const auto& message : m_messages) {
      require(message.type == 7 && message.content.empty(), "Join presentation must preserve empty protocol content");
      require(
          m_messageText.at(message.id) == joinNoticeText(message, userName(message.authorId, message.author)),
          "Selection/copy text must contain the generated notice and current display name"
      );
      const auto text = m_messageLayouts.at(message.id).layout.plainText;
      require(text.find("@") == std::string::npos, "Join names must not gain a mention prefix");
      if (m_messageTops.at(message.id) >= m_messageViewportTop)
        ++joinArrows;
    }
    require(joinArrows > 0, "Join notices must be visible");
    const auto reaction =
        std::ranges::find_if(m_hits, [](const auto& hit) { return hit.action == Action::ToggleReaction; });
    require(step == 1 || reaction != m_hits.end(), "Join notices retain their normal reaction targets");
    const auto add = std::ranges::find_if(m_hits, [](const auto& hit) { return hit.action == Action::AddReaction; });
    require(step == 1 || add != m_hits.end(), "Join reaction rows expose the native add-reaction control");
    if (step == 0) {
      m_window.click(add->x + 8, add->y + 8 + m_window.titlebarHeight() - 36, 272);
      m_window.released();
      require(m_emojiMenu, "Join reaction buttons must open the real emoji picker");
    } else if (step == 1) {
      require(m_emojiMenu, "Join reaction picker survives the next frame");
      closeEmojiPicker();
      api::UserUpdates names;
      names.users["11"].nickname = "Local nickname <b>literal</b>";
      m_userDirectory.apply(names);
      m_messageLayouts.clear();
    } else if (step == 2) {
      require(
          m_messageText.at(m_messages.front().id).find("Local nickname <b>literal</b>") != std::string::npos,
          "Live personal nicknames update system notices without markup injection"
      );
      m_preferences.compact = true;
      m_preferences.channelSidebarWidth = 420;
      m_messageLayouts.clear();
    } else {
      require(
          m_messageText.at(m_messages.front().id).find("Local nickname") != std::string::npos,
          "Join notices survive compact mode and sidebar resizing"
      );
      std::clog << "Offline server join replay passed\n";
    }
    m_window.invalidate();
  }
} // namespace app
