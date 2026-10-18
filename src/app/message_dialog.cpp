#include "app/client_application.h"
#include "app/theme.h"

#include <algorithm>
#include <cmath>

namespace app {
  void ClientApplication::drawMessageDialog(Node& root, float w, float h) {
    using namespace theme;
    if (!m_dialogAction)
      return;
    const auto message = std::ranges::find(m_messages, m_dialogAction->message, &api::Message::id);
    if (message == m_messages.end())
      return;
    const bool editing = m_dialogKind == "edit";
    const float width = std::min(500.0F, w - 32), textWidth = width - 40;
    const auto measured = m_window.target().renderer().measureText(
        editing ? m_dialogText : message->content, 14, FontWeight::Normal, textWidth - (editing ? 24 : 54), 0
    );
    const float fieldHeight =
        std::clamp(measured.bottom - measured.top + 28, 88.0F, std::max(88.0F, std::min(190.0F, h - 290)));
    const float previewHeight = std::clamp(measured.bottom - measured.top + 46, 76.0F, 112.0F);
    const float errorHeight = m_dialogError.empty() ? 0 : 38;
    const float height = (editing ? 194 + fieldHeight : 160 + previewHeight) + errorHeight;
    const float x = (w - width) / 2, y = std::max(44.0F, (h - height) / 2);
    const auto red = rgbHex(m_preferences.theme == "light" ? 0xC32B3C : 0xDA373C);
    rectangle(root, 0, 36, w, h - 36, Color{0, 0, 0, .65F});
    m_hits.push_back({0, 36, w, h - 36, Action::None, {}});
    rectangle(root, x, y, width, height, sidebar, 12);
    m_dialogBounds = {x, y, width, height, Action::None, {}};
    m_hits.push_back(m_dialogBounds);
    label(
        root, x + 20, y + 21, width - 72, editing ? "Edit message" : "Delete message", 18, foreground,
        FontWeight::SemiBold
    );
    iconButton(
        root, {x + width - 42, y + 12, 30, 30, m_dialogPending ? Action::None : Action::CloseDialog, {}}, "x", "Close"
    );
    if (editing) {
      avatar(root, x + 20, y + 59, 28, message->avatar, message->author);
      label(
          root, x + 58, y + 62, width - 78, userName(message->authorId, message->author), 13, foreground,
          FontWeight::SemiBold
      );
      auto& input =
          inputControl(root, x + 20, y + 100, textWidth, fieldHeight, "dialog", m_dialogText, "Message", true);
      input.setFrameRadius(7);
      input.setHorizontalPadding(12);
      input.setClearButtonEnabled(false);
      label(root, x + 22, y + 109 + fieldHeight, textWidth, "Enter to save · Shift+Enter for a new line", 11, muted);
    } else {
      label(root, x + 20, y + 58, textWidth, "This message will be deleted for everyone.", 13, foreground);
      auto preview = std::make_unique<Node>();
      preview->setPosition(x + 20, y + 86);
      preview->setSize(textWidth, previewHeight);
      preview->setClipChildren(true);
      rectangle(*preview, 0, 0, textWidth, previewHeight, background, 8);
      avatar(*preview, 12, 12, 32, message->avatar, message->author);
      label(
          *preview, 54, 12, textWidth - 66, userName(message->authorId, message->author), 13, foreground,
          FontWeight::SemiBold
      );
      std::string excerpt = message->content;
      if (excerpt.empty())
        excerpt = message->attachments.empty() ? "[Message]" : message->attachments.front().filename;
      label(*preview, 54, 33, textWidth - 66, excerpt, 13, muted, FontWeight::Normal, 3);
      root.addChild(std::move(preview));
    }
    if (!m_dialogError.empty())
      label(root, x + 20, y + height - 94, textWidth, m_dialogError, 12, red, FontWeight::Normal, 2);
    rectangle(root, x, y + height - 58, width, 58, panel, 12);
    rectangle(root, x, y + height - 58, width, 12, panel);
    const auto actionButton = [&](float bx, float bw, const std::string& caption, bool primary, bool enabled) {
      Hit hit{
          bx, y + height - 44, bw, 32, enabled ? (primary ? Action::SubmitDialog : Action::CloseDialog) : Action::None,
          {}
      };
      const bool over = enabled && hit.contains(m_pointerX, m_pointerY);
      const auto fill = primary ? (editing ? accent : red) : over ? raised : panel;
      rectangle(
          root, hit.x, hit.y, bw, 32,
          enabled ? (over && primary ? lerpColor(fill, foreground, .12F) : fill) : lerpColor(panel, fill, .4F), 6
      );
      const auto metrics = m_window.target().renderer().measureText(caption, 13, FontWeight::Medium);
      label(
          root, hit.x + (bw - metrics.width) / 2, hit.y + (32 - (metrics.bottom - metrics.top)) / 2, metrics.width + 2,
          caption, 13,
          !enabled       ? muted
              : !primary ? foreground
              : editing  ? ink
                         : rgbHex(0xFFFFFF),
          FontWeight::Medium
      );
      m_hits.push_back(hit);
    };
    actionButton(x + width - 212, 80, "Cancel", false, !m_dialogPending);
    actionButton(
        x + width - 126, 106,
        m_dialogPending ? (editing ? "Saving…" : "Deleting…")
            : editing   ? "Save changes"
                        : "Delete",
        true,
        !m_dialogPending
            && (!editing
                || (m_dialogText != message->content && (!m_dialogText.empty() || !message->attachments.empty())))
    );
    m_tooltip.clear();
  }
} // namespace app
