#include "app/client_application.h"
#include "app/theme.h"
#include "core/input/key_modifiers.h"
#include "core/input/keybind_matcher.h"
#include "render/scene/input_area.h"
#include "ui/controls/glyph.h"
#include "ui/palette.h"

#include <cmath>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace app {
  namespace {
    class WindowClipboard final : public TextClipboard {
      Window& m_window;

    public:
      explicit WindowClipboard(Window& window) : m_window(window) {}
      std::optional<std::string> clipboardText() override { return {}; }
      void setClipboardText(std::string text) override { m_window.copy(std::move(text)); }
    };
  } // namespace
  void ClientApplication::glyph(Node& parent, float x, float y, float size, const std::string& name, Color color) {
    auto icon = std::make_unique<Glyph>();
    icon->setGlyph(name);
    icon->setGlyphSize(size);
    icon->setColor(color);
    icon->setPosition(x, y);
    icon->setFrameSize(size, size);
    parent.addChild(std::move(icon));
  }
  void ClientApplication::iconButton(
      Node& parent, Hit hit, const std::string& icon, const std::string& tooltip, bool selected
  ) {
    float ox = 0, oy = 0;
    for (auto* node = &parent; node && node != m_scene.get(); node = node->parent()) {
      ox += node->x();
      oy += node->y();
    }
    const bool over = hovered(hit.x + ox, hit.y + oy, hit.width, hit.height);
    if (over || selected)
      rectangle(parent, hit.x, hit.y, hit.width, hit.height, over ? theme::raised : theme::panel, 7);
    const float size = std::min(20.0F, hit.height - 8);
    glyph(
        parent, hit.x + (hit.width - size) / 2, hit.y + (hit.height - size) / 2, size, icon,
        over || selected ? theme::foreground : theme::muted
    );
    hit.x += ox;
    hit.y += oy;
    m_hits.push_back(hit);
    if (over && !tooltip.empty()) {
      m_tooltip = tooltip;
      m_tooltipX = hit.x + hit.width / 2;
      m_tooltipY = hit.y;
    }
  }
  void ClientApplication::drawTooltip(Node& root, float w, float h) {
    if (m_tooltip.empty() || m_contextOpen || (!m_dialogKind.empty() && m_dialogKind != "profile"))
      return;
    auto& renderer = m_window.target().renderer();
    const auto natural =
        renderer.measureText(m_tooltip, 12, FontWeight::Normal, 0, 0, TextAlign::Start, {}, TextEllipsize::End);
    const float width = std::min(std::min(480.0F, w - 16), std::ceil(natural.width) + 20);
    const auto metrics = renderer.measureText(
        m_tooltip, 12, FontWeight::Normal, width - 20, 0, TextAlign::Start, {}, TextEllipsize::End
    );
    const float height = std::ceil(metrics.bottom - metrics.top) + 16;
    const float x = std::clamp(m_tooltipX - width / 2, 8.0F, std::max(8.0F, w - width - 8));
    const float preferredY = m_tooltipY - height - 8 >= 40 ? m_tooltipY - height - 8 : m_tooltipY + 38;
    const float y = std::clamp(preferredY, 40.0F, std::max(40.0F, h - height - 8));
    rectangle(root, x, y, width, height, theme::raised, 6);
    label(root, x + 10, y + 8, width - 20, m_tooltip, 12, theme::foreground, FontWeight::Normal, 0);
  }
  ClientApplication::~ClientApplication() {
    closeVideo();
    m_animationTimer.stop();
    m_gifSearchTimer.stop();
    m_reactorHoverTimer.stop();
    // Destroy tracked input areas while their dispatcher and callback state live.
    m_buildingScene = true;
    m_controlInput.setSceneRoot(nullptr);
    m_scene.reset();
    m_detachedInputs.clear();
    m_inputs.clear();
    m_widgets.clear();
    Input::setTextClipboard(nullptr);
  }
  void ClientApplication::beginControls() {
    m_buildingScene = true;
    if (!m_controlClipboard) {
      m_controlClipboard = std::make_unique<WindowClipboard>(m_window);
      Input::setTextClipboard(m_controlClipboard.get());
      m_controlInput.setFocusChangeCallback([this](InputArea*, InputArea* area) {
        m_focusedInput.clear();
        m_fieldFocused = false;
        m_settingsField.clear();
        for (const auto& [id, input] : m_inputs)
          if (input->inputArea() == area) {
            m_focusedInput = id;
            m_fieldFocused = id == "composer" || id == "token";
            if (id.starts_with("settings:"))
              m_settingsField = id.substr(9);
            break;
          }
      });
      Input::setValidateKeyMatcher([](std::uint32_t key, std::uint32_t) {
        return key == XKB_KEY_Return || key == XKB_KEY_KP_Enter;
      });
      KeybindMatcher::setMatcher(KeybindAction::Validate, [](std::uint32_t key, std::uint32_t modifiers) {
        return modifiers == 0 && (key == XKB_KEY_Return || key == XKB_KEY_KP_Enter || key == XKB_KEY_space);
      });
    }
    for (const auto& [id, input] : m_inputs)
      if (input->parent())
        m_detachedInputs[id] = input->parent()->removeChild(input);
    for (const auto& [id, control] : m_widgets)
      if (control->parent())
        m_detachedInputs[id] = control->parent()->removeChild(control);
    using namespace theme;
    setPalette(
        {accent, ink, accent, ink, accent, ink, rgbHex(0xED4245), rgbHex(0xFFFFFF), background, foreground, panel,
         muted, raised, rgbHex(0x000000), raised, foreground}
    );
  }
  void ClientApplication::focusControl(const std::string& id) {
    auto found = m_inputs.find(id);
    if (found != m_inputs.end() && found->second->parent()) {
      m_focusedInput = id;
      m_controlInput.setFocus(found->second->inputArea());
    }
  }
  void ClientApplication::finishControls() {
    m_scene->setInvalidationCallback([this](NodeInvalidation) {
      if (!m_buildingScene)
        m_window.invalidate();
    });
    m_scene->layout(m_window.target().renderer());
    m_controlInput.setSceneRoot(m_scene.get());
    if (!m_focusedInput.empty()) {
      const auto input = m_inputs.find(m_focusedInput);
      if (input != m_inputs.end() && input->second->parent())
        focusControl(m_focusedInput);
      else {
        m_controlInput.setFocus(nullptr);
        m_focusedInput.clear();
      }
    }
    m_buildingScene = false;
  }
  void ClientApplication::pasteControl(const std::string& id, std::string text) {
    auto input = m_inputs.find(id);
    if (input == m_inputs.end() || !input->second->parent() || !g_utf8_validate(text.data(), text.size(), nullptr))
      return;
    const bool multiline = id == "composer"
        || id == "forum-body"
        || id == "channel-topic"
        || id == "settings:bio"
        || id == "message-edit"
        || (id.starts_with("application-field:") && m_dialogKind == "application-modal");
    std::erase_if(text, [&](unsigned char c) {
      return (c < 32 && !(multiline && (c == '\n' || c == '\t'))) || c == 127;
    });
    TextInputEdit edit;
    edit.hasCommitText = true;
    edit.commitText = std::move(text);
    input->second->textInputApplyEdit(edit);
    m_window.invalidate();
  }
  Input& ClientApplication::inputControl(
      Node& parent, float x, float y, float width, float height, const std::string& id, const std::string& value,
      const std::string& placeholder, bool multiline, bool password
  ) {
    std::unique_ptr<Node> node;
    if (auto stored = m_detachedInputs.find(id); stored != m_detachedInputs.end()) {
      node = std::move(stored->second);
      m_detachedInputs.erase(stored);
    } else {
      auto field = std::make_unique<Input>();
      field->setFontSize(14);
      field->setMultiline(multiline);
      field->setMultilineCenterWhenFits(id == "composer");
      if (id == "composer")
        field->setHorizontalPadding(4);
      field->setPasswordMode(password);
      field->setSubmitOnEnter(id == "composer");
      field->setOnFocusGain([this, id] {
        m_focusedInput = id;
        m_fieldFocused = id == "composer" || id == "token";
        m_settingsField = id.starts_with("settings:") ? id.substr(9) : "";
      });
      field->setOnKeyEvent([this, id](std::uint32_t key, std::uint32_t modifiers) {
        if (((modifiers & KeyMod::Ctrl) && (key == XKB_KEY_v || key == XKB_KEY_V))
            || ((modifiers & KeyMod::Shift) && key == XKB_KEY_Insert)) {
          m_pasteInput = id;
          m_pasteDialogGeneration = m_dialogGeneration;
          m_pasteMessageEditGeneration = m_messageEdit ? m_messageEdit->generation : 0;
          m_pasteConnected = m_connected;
          m_pasteSession = m_session;
          m_pasteChannel = m_channelId;
          m_window.paste(id == "composer" && !(modifiers & KeyMod::Shift));
          return true;
        }
        return false;
      });
      field->setOnChange([this, id](const std::string& text) {
        if (id == "composer") {
          if (text.size() <= 8000) {
            m_drafts[m_channelId] = text;
            m_edit.end(text);
          }
        } else if (id == "message-edit") {
          if (m_messageEdit && !m_messageEdit->request && text.size() <= 8000) {
            m_messageEdit->text = text;
            m_messageEdit->error.clear();
          }
        } else if (id == "emoji-search") {
          if (text.size() <= 256) {
            m_emojiQuery = text;
            m_emojiJump.clear();
            m_scrolling.stop(m_emojiScroll);
            m_emojiScroll = 0;
          }
        } else if (id == "channel-search" || id == "channel-search-panel") {
          if (text.size() <= 1024)
            m_channelSearchText = text;
        } else if (id == "channel-name")
          m_channelFormName = text;
        else if (id == "channel-topic")
          m_channelFormTopic = text;
        else if (id == "channel-access-filter") {
          m_channelAccessFilter = text;
          m_scrolling.stop(m_channelDialogScroll);
          m_channelDialogScroll = 0;
        } else if (id.starts_with("application-field:")) {
          if (m_applicationModal && m_dialogKind == "application-modal" && !m_dialogPending)
            for (std::size_t i = 0; i < m_applicationValues.size(); ++i)
              if (id == "application-field:" + std::to_string(i) && text.size() <= 16000)
                m_applicationValues[i] = text;
        } else if (id == "forum-search") {
          if (text.size() <= 400 && api::utf8Length(text) <= 100) {
            m_forumSearch = text;
            updateForumSearch();
          }
        } else if (id == "forum-body") {
          if (text.size() <= 8000)
            m_drafts[m_channelId] = text;
        } else if (id == "gif-search") {
          if (text.size() <= 256) {
            m_gifQuery = text;
            m_gifScroll = 0;
            m_gifSearchTimer.stop();
            if (!m_gifFavoritesTab && !m_demo)
              m_gifSearchTimer.start(std::chrono::milliseconds(300), [this] { searchGifs(); });
          }
        } else if (id == "dialog") {
          m_dialogText = text;
        } else if (id == "token") {
          if (text.size() <= 4096)
            m_token = text;
        } else if (id.starts_with("settings:")) {
          const auto key = id.substr(9);
          const auto limit = key == "display_name" ? 32U : key == "pronouns" ? 40U : key == "bio" ? 190U : 128U;
          if (api::utf8Length(text) <= limit) {
            m_settingsFields[key] = text;
            if (key != "custom_status" && key != "search")
              m_profileDirty = true;
            if (key == "search")
              m_settingsNavScroll = 0;
          }
        }
        m_window.invalidate();
      });
      field->setOnSubmit([this, id](const std::string&) {
        if (id == "composer")
          send();
        else if (id == "message-edit")
          submitMessageEdit();
        else if (id == "emoji-search") {
          // Selection is explicit; Enter must not submit account settings.
        } else if (id == "channel-search" || id == "channel-search-panel")
          searchChannel();
        else if (
            id == "channel-access-filter"
            || id == "channel-name"
            || id == "channel-topic"
            || id.starts_with("application-field:")
        ) {
          // Forms submit through their explicit Save/Submit action.
        } else if (id == "gif-search")
          searchGifs();
        else if (id == "forum-search") {
          m_forumSearchTimer.stop();
          loadThreads(true);
        } else if (id == "forum-body" || id == "dialog")
          submitDialog();
        else if (id == "token")
          connect();
        else if (id == "settings:custom_status")
          setPresence(api::validStatus(m_presence.status) ? m_presence.status : "online", true);
        else if (id != "settings:search")
          activate({0, 0, 0, 0, Action::SaveProfile, {}});
      });
      m_inputs[id] = field.get();
      node = std::move(field);
    }
    auto& input = *static_cast<Input*>(node.get());
    input.setMultiline(multiline);
    input.setSubmitOnEnter(id == "composer" || id == "dialog" || id == "message-edit");
    if (input.value() != value)
      input.setValue(value);
    input.setPlaceholder(placeholder);
    input.setFrameVisible(id != "composer");
    input.setEnabled(
        (m_dialogKind.empty() || id == "dialog")
        && (id == "dialog"             ? !m_dialogPending
                : id == "message-edit" ? m_messageEdit && !m_messageEdit->request
                : id == "token"        ? !m_connecting
                : id == "composer" ? !m_demo || m_demoOverlay == "sidebar-cycle" || m_demoOverlay == "inline-edit-cycle"
                                   : !m_savingProfile)
    );
    input.setControlHeight(height);
    input.setPosition(x, y);
    input.setFrameSize(width, height);
    parent.addChild(std::move(node));
    return input;
  }
} // namespace app
