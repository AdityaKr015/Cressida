#include "app/client_application.h"
#include "app/theme.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace app {
  bool ClientApplication::beginMessageEdit(const std::string& id) {
    const auto message = std::ranges::find(m_messages, id, &api::Message::id);
    if (message == m_messages.end()
        || message->authorId != m_account.id
        || message->deleted
        || !message->webhookId.empty()
        || (message->flags & 64)
        || (message->type != 0 && message->type != 19)
        || (m_messageEdit && m_messageEdit->request))
      return false;
    cancelMessageEdit(false);
    m_messageEdit = MessageEditState{m_channelId, id, message->content, {}, {}, ++m_messageEditGeneration};
    closeGifPicker();
    closeEmojiPicker();
    m_controlInput.setFocus(nullptr);
    m_focusedInput = "message-edit";
    m_fieldFocused = false;
    m_dragDraft = m_dragMessage = false;
    m_scrolling.stop(m_messageScroll);
    m_seekMessage = id;
    m_window.invalidate();
    return true;
  }

  void ClientApplication::cancelMessageEdit(bool restoreComposer) {
    if (!m_messageEdit)
      return;
    if (m_completionInput == "message-edit")
      closeCompletion();
    const bool focused = m_focusedInput == "message-edit";
    m_messageEdit.reset();
    ++m_messageEditGeneration;
    if (m_inputs.contains("message-edit"))
      m_inputs.at("message-edit")->setValue("");
    if (focused) {
      m_controlInput.setFocus(nullptr);
      m_focusedInput = restoreComposer ? "composer" : "";
      m_fieldFocused = restoreComposer;
    }
    m_window.invalidate();
  }

  bool ClientApplication::messageEditKey(const Key& key) {
    if (!m_connected
        || settingsVisible()
        || !m_dialogKind.empty()
        || m_contextOpen
        || m_emojiMenu
        || m_gifMenu
        || m_presenceMenu
        || forumView())
      return false;
    if (m_messageEdit && m_focusedInput == "message-edit" && key.symbol == XKB_KEY_Escape) {
      if (!m_messageEdit->request)
        cancelMessageEdit();
      return true;
    }
    if (key.symbol != XKB_KEY_Up
        || key.control
        || key.shift
        || m_messageEdit
        || m_sending
        || m_loadingMessages
        || (!m_demo && !m_history.initialized())
        || !m_history.atLatest()
        || m_channelId.empty()
        || !m_drafts[m_channelId].empty()
        || !m_attachments[m_channelId].empty()
        || !(m_focusedInput == "composer" || (m_focusedInput.empty() && m_fieldFocused)))
      return false;
    for (auto message = m_messages.rbegin(); message != m_messages.rend(); ++message)
      if (message->authorId == m_account.id && !message->content.empty() && beginMessageEdit(message->id))
        return true;
    return false;
  }

  void ClientApplication::submitMessageEdit() {
    if (!m_messageEdit || m_messageEdit->request || m_messageEdit->channel != m_channelId)
      return;
    const auto message = std::ranges::find(m_messages, m_messageEdit->message, &api::Message::id);
    if (message == m_messages.end() || message->deleted || message->authorId != m_account.id) {
      cancelMessageEdit();
      return;
    }
    if (message->content == m_messageEdit->text) {
      cancelMessageEdit();
      return;
    }
    if (m_messageEdit->text.size() > 8000 || (m_messageEdit->text.empty() && message->attachments.empty())) {
      m_messageEdit->error = m_messageEdit->text.empty() ? "Messages cannot be empty." : "This message is too long.";
      m_window.invalidate();
      return;
    }
    api::Interaction action{
        .kind = api::InteractionKind::EditMessage,
        .channel = m_channelId,
        .message = message->id,
        .text = m_emojiCatalog.messageContent(
            m_messageEdit->text, m_dmMode ? "" : m_guildId, m_account.premiumType.value_or(0),
            m_preferences.emojiImageLinks
        ),
        .revision = ++m_messageEditGeneration
    };
    if (startInteraction(action)) {
      m_messageEdit->request = std::move(action);
      m_messageEdit->error.clear();
    } else {
      m_messageEdit->error = m_status;
      m_status.clear();
    }
    m_window.invalidate();
  }

  float ClientApplication::messageEditHeight(float width) {
    if (!m_messageEdit)
      return 0;
    const auto measured = m_window.target().renderer().measureText(
        m_messageEdit->text.empty() ? " " : m_messageEdit->text, m_preferences.messageSize, FontWeight::Normal,
        std::max(1.0F, width - 24), 0
    );
    const float field = std::clamp(measured.bottom - measured.top + 24, 44.0F, 180.0F);
    return field + 30 + (m_messageEdit->error.empty() ? 0 : 34);
  }

  void ClientApplication::drawMessageEdit(Node& parent, float x, float y, float width) {
    using namespace theme;
    if (!m_messageEdit)
      return;
    const float fieldHeight = messageEditHeight(width) - 30 - (m_messageEdit->error.empty() ? 0 : 34);
    auto& input = inputControl(parent, x, y, width, fieldHeight, "message-edit", m_messageEdit->text, "Message", true);
    input.setFontSize(m_preferences.messageSize);
    input.setMultilineCenterWhenFits(true);
    input.setFrameRadius(6);
    input.setHorizontalPadding(12);
    input.setClearButtonEnabled(false);
    const float footerY = y + fieldHeight + 4;
    if (width > 320)
      label(parent, x + 2, footerY + 6, width - 150, "Escape to cancel · Enter to save", 10, muted);
    const auto control = [&](float bx, float bw, const std::string& text, Action action, bool primary) {
      const bool enabled = !m_messageEdit->request;
      Hit hit{bx, footerY, bw, 24, enabled ? action : Action::None, {}};
      // iconButton/button translate nested hits; this small text control does the same.
      float ox = 0, oy = 0;
      for (auto* node = &parent; node && node != m_scene.get(); node = node->parent()) {
        ox += node->x();
        oy += node->y();
      }
      const bool over = enabled && hovered(bx + ox, footerY + oy, bw, 24);
      rectangle(parent, bx, footerY, bw, 24, primary ? accent : over ? raised : panel, 4);
      const auto measured = m_window.target().renderer().measureText(text, 12, FontWeight::Medium);
      label(
          parent, bx + (bw - measured.width) / 2, footerY + (24 - (measured.bottom - measured.top)) / 2,
          measured.width + 2, text, 12,
          primary       ? ink
              : enabled ? foreground
                        : muted,
          FontWeight::Medium
      );
      hit.x += ox;
      hit.y += oy;
      m_hits.push_back(std::move(hit));
    };
    control(x + width - 136, 58, "Cancel", Action::CancelMessageEdit, false);
    control(x + width - 72, 72, m_messageEdit->request ? "Saving…" : "Save", Action::SaveMessageEdit, true);
    if (!m_messageEdit->error.empty())
      label(parent, x + 2, footerY + 29, width - 4, m_messageEdit->error, 11, theme::danger, FontWeight::Normal, 2);
  }

  void ClientApplication::demoMessageEditStep() {
    const auto require = [](bool value, const char* reason) {
      if (!value)
        throw std::runtime_error(reason);
    };
    const auto step = m_demoInteractionStep++;
    if (step == 0) {
      onKey({XKB_KEY_Up});
      require(
          m_messageEdit && m_messageEdit->message == "9803" && m_dialogKind.empty(),
          "Up edits the latest ordinary own message, skipping deleted/system rows, without a dialog"
      );
    } else if (step == 1) {
      require(
          m_inputs.at("message-edit")->parent() && m_focusedInput == "message-edit",
          "The inline editor retains native keyboard focus in the message row"
      );
      pasteControl("message-edit", "\nPasted line");
      onKey({.symbol = XKB_KEY_Return, .shift = true});
      require(m_messageEdit->text.ends_with("Pasted line\n"), "Inline paste and Shift+Enter preserve newlines");
      onKey({XKB_KEY_Escape});
      require(
          !m_messageEdit && m_drafts[m_channelId].empty() && m_inputs.at("message-edit")->value().empty(),
          "Escape clears edit input/undo without changing the composer"
      );
    } else if (step == 2) {
      m_drafts[m_channelId] = "An unsent draft";
      m_inputs.at("composer")->setValue(m_drafts[m_channelId]);
      focusControl("composer");
      onKey({XKB_KEY_Up});
      require(
          !m_messageEdit && m_drafts[m_channelId] == "An unsent draft",
          "Up keeps ordinary composer navigation for nonempty drafts"
      );
      m_drafts[m_channelId].clear();
      m_inputs.at("composer")->setValue("");
      m_contextMessage = "9801";
      m_contextChannel = m_channelId;
      contextAction(Action::EditMessage);
      require(
          m_messageEdit && m_messageEdit->message == "9801",
          "Context Edit opens the same inline editor for older messages"
      );
    } else if (step == 3) {
      m_inputs.at("message-edit")->selectAll();
      pasteControl("message-edit", "Changed inline\nwith a second line.");
      onKey({XKB_KEY_Return});
      require(
          m_messageEdit && !m_messageEdit->error.empty() && !m_messageEdit->request,
          "Enter submits through the existing action path; demo remains read-only and preserves the draft"
      );
      api::Event failed;
      failed.operation = api::Operation::Interaction;
      failed.interaction = api::Interaction{
          .kind = api::InteractionKind::EditMessage,
          .channel = m_channelId,
          .message = "9801",
          .text = m_messageEdit->text,
          .revision = ++m_messageEditGeneration
      };
      m_messageEdit->request = failed.interaction;
      failed.error = "Could not save. Try again.";
      interactionResult(failed);
      require(
          m_messageEdit
              && !m_messageEdit->request
              && m_messageEdit->error == failed.error
              && m_messageEdit->text == "Changed inline\nwith a second line.",
          "Failed acknowledgement preserves the editor draft and error for explicit retry"
      );
    } else if (step == 4) {
      updateMessage(
          m_channelId, "9801",
          {.content = "A live edit from another device.", .editedTimestamp = "2026-10-18T12:03:00Z"}
      );
      require(
          m_messageEdit->text == "Changed inline\nwith a second line.", "Live updates cannot replace an unsaved draft"
      );
      api::Event saved;
      saved.operation = api::Operation::Interaction;
      saved.interaction = api::Interaction{
          .kind = api::InteractionKind::EditMessage,
          .channel = m_channelId,
          .message = "9801",
          .text = m_messageEdit->text,
          .revision = ++m_messageEditGeneration
      };
      m_messageEdit->request = saved.interaction;
      auto message = m_messages.front();
      message.content = m_messageEdit->text;
      message.editedTimestamp = "2026-10-18T12:04:00Z";
      saved.messages = {message};
      interactionResult(saved);
      require(
          !m_messageEdit
              && m_messages.front().content == message.content
              && !m_messageLog.edits(m_channelId, "9801").empty(),
          "Successful acknowledgement closes the editor and merges through edit history"
      );
    } else if (step == 5) {
      beginMessageEdit("9803");
      deleteMessages(m_channelId, {"9803"});
      require(
          !m_messageEdit && m_inputs.at("message-edit")->value().empty(),
          "Live deletion clears the inline editor immediately"
      );
      beginMessageEdit("9801");
      const auto staleGeneration = m_messageEdit->generation;
      cancelMessageEdit();
      beginMessageEdit("9801");
      const auto draft = m_messageEdit->text;
      m_pasteInput = "message-edit";
      m_pasteMessageEditGeneration = staleGeneration;
      m_pasteConnected = m_connected;
      m_pasteSession = m_session;
      m_pasteChannel = m_channelId;
      m_window.pasted("Stale clipboard", "text/plain");
      require(m_messageEdit->text == draft, "Clipboard replies cannot enter a reopened edit generation");
    } else if (step == 6) {
      const auto originalChannel = m_channelId;
      auto original = m_messages.front();
      api::Interaction late{
          .kind = api::InteractionKind::EditMessage,
          .channel = originalChannel,
          .message = original.id,
          .text = "A late saved edit.",
          .revision = ++m_messageEditGeneration
      };
      m_messageEdit->request = late;
      selectChannel(m_channels.back().id);
      require(
          !m_messageEdit && m_inputs.at("message-edit")->value().empty(),
          "Navigation clears draft and native edit history"
      );
      auto next = original;
      next.id = "9891";
      next.channelId = m_channelId;
      m_messages = {next};
      m_messageLayouts.clear();
      beginMessageEdit(next.id);
      api::Event event;
      event.operation = api::Operation::Interaction;
      event.interaction = late;
      original.content = late.text;
      original.editedTimestamp = "2026-10-18T12:05:00Z";
      event.messages = {original};
      interactionResult(event);
      require(
          m_messageEdit && m_messageEdit->message == next.id && m_messageEdit->text == next.content,
          "Late save acknowledgements cannot close or replace a different editor"
      );
    } else {
      require(m_messageEdit && m_inputs.at("message-edit")->parent(), "The new channel editor is rendered inline");
      openSettings();
      require(
          !m_messageEdit && m_inputs.at("message-edit")->value().empty(), "Opening settings clears inline edit state"
      );
    }
    m_window.invalidate();
  }

  void ClientApplication::drawMessageDialog(Node& root, float w, float h) {
    using namespace theme;
    if (!m_dialogAction)
      return;
    const auto message = std::ranges::find(m_messages, m_dialogAction->message, &api::Message::id);
    if (message == m_messages.end())
      return;
    const float width = std::min(500.0F, w - 32), textWidth = width - 40;
    const auto measured =
        m_window.target().renderer().measureText(message->content, 14, FontWeight::Normal, textWidth - 54, 0);
    const float previewHeight = std::clamp(measured.bottom - measured.top + 46, 76.0F, 112.0F);
    const float errorHeight = m_dialogError.empty() ? 0 : 38;
    const float height = (160 + previewHeight) + errorHeight;
    const float x = (w - width) / 2, y = std::max(44.0F, (h - height) / 2);
    const auto red = theme::destructive;
    rectangle(root, 0, 36, w, h - 36, Color{0, 0, 0, .65F});
    m_hits.push_back({0, 36, w, h - 36, Action::None, {}});
    rectangle(root, x, y, width, height, sidebar, 12);
    m_dialogBounds = {x, y, width, height, Action::None, {}};
    m_hits.push_back(m_dialogBounds);
    label(root, x + 20, y + 21, width - 72, "Delete message", 18, foreground, FontWeight::SemiBold);
    iconButton(
        root, {x + width - 42, y + 12, 30, 30, m_dialogPending ? Action::None : Action::CloseDialog, {}}, "x", "Close"
    );
    {
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
      const auto fill = primary ? red : over ? raised : panel;
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
                         : rgbHex(0xFFFFFF),
          FontWeight::Medium
      );
      m_hits.push_back(hit);
    };
    actionButton(x + width - 212, 80, "Cancel", false, !m_dialogPending);
    actionButton(x + width - 126, 106, m_dialogPending ? "Deleting…" : "Delete", true, !m_dialogPending);
    m_tooltip.clear();
  }
} // namespace app
