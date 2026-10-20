#include "app/client_application.h"
#include "cursor-shape-v1-client-protocol.h"
#include "render/core/image_encoder.h"
#include "render/core/texture_manager.h"
#include "render/scene/image_node.h"
#include "render/scene/input_area.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace app {
  namespace {
    constexpr std::string_view draftImagePrefix = "draft-attachment:";
  }
  const ClientApplication::DraftImage* ClientApplication::draftImage(std::string_view path) const {
    if (!path.starts_with(draftImagePrefix))
      return nullptr;
    const auto draft = m_attachments.find(m_channelId);
    if (draft == m_attachments.end())
      return nullptr;
    const auto image = std::ranges::find(draft->second, path.substr(draftImagePrefix.size()), &DraftImage::key);
    return image == draft->second.end() ? nullptr : &*image;
  }
  void ClientApplication::openDraftAttachment(const std::string& key) {
    const auto path = std::string(draftImagePrefix) + key;
    const auto* selected = draftImage(path);
    if (!selected || !selected->texture.valid())
      return;
    m_viewerImages.clear();
    for (const auto& image : m_attachments.at(m_channelId))
      if (image.texture.valid())
        m_viewerImages.push_back(
            {std::string(draftImagePrefix) + image.key,
             {},
             static_cast<float>(image.texture.width),
             static_cast<float>(image.texture.height)}
        );
    activate({.action = Action::ViewImage, .id = path});
  }
  bool ClientApplication::hovered(float x, float y, float width, float height) const {
    return m_pointerX >= x && m_pointerY >= y && m_pointerX < x + width && m_pointerY < y + height;
  }
  void ClientApplication::updateCursor() {
    std::uint32_t shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    if (m_resizingSidebar)
      shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COL_RESIZE;
    else if (m_dragImage)
      shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING;
    else if (m_dragDraft || m_dragMessage)
      shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
    else if (m_controlInput.pointerCaptured())
      shape = m_controlCursor;
    else if (
        userAccountDialogOpen()
        && !(m_selectRequest && m_selectBounds.contains(m_pointerX, m_pointerY))
        && (m_pointerX < m_userAccount.dialogX
            || m_pointerY < m_userAccount.dialogY
            || m_pointerX >= m_userAccount.dialogX + m_userAccount.dialogWidth
            || m_pointerY >= m_userAccount.dialogY + m_userAccount.dialogHeight)
    )
      shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    else if (sidebarResizeAt(m_pointerX, m_pointerY))
      shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COL_RESIZE;
    else {
      for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it)
        if (it->contains(m_pointerX, m_pointerY)) {
          switch (it->action) {
          case Action::None:
            break;
          case Action::Composer:
          case Action::Token:
          case Action::SelectMessage:
            shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
            break;
          case Action::ImagePan:
            shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB;
            break;
          default:
            shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
            break;
          }
          break;
        }
      auto* area = m_controlInput.inputAreaAt(
          m_pointerX, m_pointerY + m_window.titlebarHeight() - layout::Window::titlebarHeight
      );
      // Covered native controls remain in the scene. Only the active overlay
      // may supply a cursor; InputAreas are children of retained controls.
      const auto allowed = [&](const std::string& id) {
        if (!m_viewImage.empty() || m_videoExpanded || m_presenceMenu)
          return false;
        if (m_selectRequest)
          return id.starts_with("select-popup") && m_selectBounds.contains(m_pointerX, m_pointerY);
        if (userAccountDialogOpen())
          return id.starts_with("user-account:secret:")
              || id.starts_with("user-account-dialog-button:")
              || id == "user-account-provider-select"
              || id == "user-account-dialog-scrollbar";
        if (m_serverSettings.open)
          return id.starts_with("server-");
        if (m_contextOpen)
          return id.starts_with("popup:")
              && (m_contextBounds.contains(m_pointerX, m_pointerY)
                  || (m_reactionSubmenu && m_reactionSubmenuBounds.contains(m_pointerX, m_pointerY)));
        if (!m_dialogKind.empty())
          return (id == "invite:options"
                  && m_channelInvite.dropdown
                  && m_channelInvite.dropdownBounds.contains(m_pointerX, m_pointerY))
              || (m_dialogBounds.contains(m_pointerX, m_pointerY)
                  && (id == "dialog"
                      || id.starts_with("server-menu:")
                      || id.starts_with("toggle:server-menu:")
                      || id == "channel-name"
                      || id == "channel-topic"
                      || id == "channel-access-filter"
                      || id.starts_with("channel-create")
                      || id.starts_with("toggle:channel-create")
                      || id.starts_with("channel-edit:")
                      || id.starts_with("invite:")
                      || id.starts_with("application-")
                      || id.starts_with("forum-")
                      || id == "profile-scrollbar"));
        if (m_emojiMenu)
          return (id == "emoji-search" || (m_stickerTab && (id == "sticker-scroll" || id == "sticker-rail-scroll")))
              && m_emojiBounds.contains(m_pointerX, m_pointerY);
        if (m_gifMenu)
          return id == "gif-search" && m_gifBounds.contains(m_pointerX, m_pointerY);
        if (m_completionQuery && m_completionBounds.contains(m_pointerX, m_pointerY))
          return id == "completion-scrollbar";
        if (m_settingsOpen && m_fontPickerOpen)
          return m_fontPickerBounds.contains(m_pointerX, m_pointerY)
              && (id == "settings:font_search" || id == "font-picker-scrollbar");
        return true;
      };
      bool nativeAllowed = false;
      for (auto* node = static_cast<Node*>(area); node && !nativeAllowed; node = node->parent()) {
        for (const auto& [id, control] : m_inputs)
          if (node == control && allowed(id))
            nativeAllowed = true;
        for (const auto& [id, control] : m_widgets)
          if (node == control && allowed(id))
            nativeAllowed = true;
      }
      if (nativeAllowed)
        shape = area->cursorShape() ? area->cursorShape() : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    }
    m_window.setCursorShape(shape);
  }

  void ClientApplication::changeImage(int direction) {
    if (m_viewerImages.empty())
      return;
    auto it = std::ranges::find(m_viewerImages, m_viewImage, &ViewerImage::path);
    const int current = it == m_viewerImages.end() ? 0 : it - m_viewerImages.begin();
    const auto& next = m_viewerImages[(current + direction + m_viewerImages.size()) % m_viewerImages.size()];
    m_viewImage = next.path;
    m_viewUrl = next.url;
    m_viewZoom = 1;
    m_viewPanX = m_viewPanY = 0;
  }
  void ClientApplication::drawImageViewer(Node& root, float w, float h) {
    const auto* draft = draftImage(m_viewImage);
    if (m_viewImage.starts_with(draftImagePrefix) && !draft) {
      // A send acknowledgement or removal can release this texture while the
      // viewer is open. Resolve it from the draft on each frame; never own it twice.
      m_viewImage.clear();
      m_viewerImages.clear();
      m_window.invalidate();
      return;
    }
    std::erase_if(m_hits, [](const auto& hit) {
      return hit.action != Action::Close && hit.action != Action::Minimize && hit.action != Action::Maximize;
    });
    Color backdrop = rgbHex(0x08090D);
    backdrop.a = 0.96F;
    rectangle(root, 0, 36, w, h - layout::Window::titlebarHeight, backdrop);
    m_hits.push_back({0, 36, w, h - layout::Window::titlebarHeight, Action::CloseImage, {}});
    const float left = 60, top = 94, availableW = w - 120, availableH = h - 174;
    const auto found = m_images.find(m_viewImage);
    const auto texture = draft ? draft->texture : found == m_images.end() ? TextureHandle{} : found->second.texture;
    float iw = texture.valid() ? texture.width : 640, ih = texture.valid() ? texture.height : 480;
    const auto metadata = std::ranges::find(m_viewerImages, m_viewImage, &ViewerImage::path);
    if (metadata != m_viewerImages.end() && metadata->width > 0 && metadata->height > 0) {
      iw = metadata->width;
      ih = metadata->height;
    }
    const float scale = std::min({availableW / iw, availableH / ih, 1.0F}) * m_viewZoom;
    iw *= scale;
    ih *= scale;
    m_viewPanX = std::clamp(m_viewPanX, -std::max(0.0F, (iw - availableW) / 2), std::max(0.0F, (iw - availableW) / 2));
    m_viewPanY = std::clamp(m_viewPanY, -std::max(0.0F, (ih - availableH) / 2), std::max(0.0F, (ih - availableH) / 2));
    auto imageLayer = std::make_unique<Node>();
    imageLayer->setPosition(left, top);
    imageLayer->setSize(availableW, availableH);
    imageLayer->setClipChildren(true);
    const float x = (availableW - iw) / 2 + m_viewPanX, y = (availableH - ih) / 2 + m_viewPanY;
    if (draft) {
      auto image = std::make_unique<ImageNode>();
      image->setPosition(x, y);
      image->setSize(iw, ih);
      image->setTextureId(texture.id);
      image->setTextureSize(texture.width, texture.height);
      image->setFitMode(ImageFitMode::Contain);
      image->setRadius(Style::scaledRadius(Style::surfaceRadius));
      imageLayer->addChild(std::move(image));
    } else
      picture(*imageLayer, x, y, iw, ih, m_viewImage, Style::surfaceRadius, true);
    root.addChild(std::move(imageLayer));
    const float hitX = std::max(left, left + x), hitY = std::max(top, top + y);
    const float hitRight = std::min(left + availableW, left + x + iw),
                hitBottom = std::min(top + availableH, top + y + ih);
    m_hits.push_back(
        {hitX, hitY, std::max(0.0F, hitRight - hitX), std::max(0.0F, hitBottom - hitY), Action::ImagePan, {}}
    );
    label(
        root, 28, 53, w - 140, draft ? draft->upload.filename : "Image", Style::fontSizeSection, rgbHex(0xEEEFF5),
        FontWeight::SemiBold
    );
    iconButton(root, {w - 64, 46, 32, 32, Action::CloseImage, {}}, "x", "Close image");
    if (m_viewerImages.size() > 1) {
      iconButton(
          root, {12, top + availableH / 2 - 20, 32, 32, Action::ImagePrevious, {}}, "chevron-left", "Previous image"
      );
      iconButton(
          root, {w - 54, top + availableH / 2 - 20, 32, 32, Action::ImageNext, {}}, "chevron-right", "Next image"
      );
    }
    iconButton(root, {w / 2 - 118, h - 62, 32, 32, Action::ImageZoom, "out"}, "zoom-out", "Zoom out");
    button(
        root, {w / 2 - 68, h - 62, 80, 34, Action::ImageZoom, "fit"},
        std::to_string(static_cast<int>(m_viewZoom * 100)) + "%"
    );
    iconButton(root, {w / 2 + 20, h - 62, 32, 32, Action::ImageZoom, "in"}, "zoom-in", "Zoom in");
    if (!m_viewUrl.empty())
      button(root, {w - 182, h - 62, 156, 34, Action::OpenUrl, m_viewUrl}, "Open original");
  }
  void ClientApplication::prepareAttachmentsDemo() {
    constexpr int width = 160, height = 320, count = 10;
    std::vector<std::uint8_t> pixels(width * height * 4);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        const auto at = (y * width + x) * 4;
        pixels[at] = x % 2 ? 180 : 70;
        pixels[at + 1] = y % 2 ? 150 : 80;
        pixels[at + 2] = 210;
        pixels[at + 3] = 255;
      }
    const auto bytes = encodePng(pixels.data(), width, height);
    for (int index = 0; index < count; ++index) {
      const auto key = std::to_string(++m_attachmentSerial);
      const auto texture = m_window.context().textureManager().loadFromRgba(pixels.data(), width, height);
      m_attachments[m_channelId].push_back({{"draft-" + key + ".png", "image/png", bytes}, texture, key});
    }
    m_drafts[m_channelId] = "Unsent draft";
  }
  void ClientApplication::demoAttachmentsStep() {
    const auto step = m_demoInteractionStep++;
    const auto require = [](bool value, const char* message) {
      if (!value)
        throw std::runtime_error(message);
    };
    const auto click = [&](Action action, const std::string& key = {}) {
      const auto hit = std::ranges::find_if(m_hits, [&](const auto& value) {
        return value.action == action && (key.empty() || value.id == key) && value.width > 0;
      });
      require(hit != m_hits.end(), "Draft attachment replay needs a visible control");
      const auto x = hit->x + hit->width / 2;
      const auto y = hit->y + hit->height / 2 + m_window.titlebarHeight() - layout::Window::titlebarHeight;
      m_window.motion(x, y);
      m_window.click(x, y, 272);
      m_window.released();
    };
    if (step == 0) {
      click(Action::ViewAttachment, "1");
      require(draftImage(m_viewImage) != nullptr && m_viewUrl.empty(), "Draft images open locally without a URL");
      require(m_viewerImages.size() == 10, "The draft viewer contains the draft image gallery");
    } else if (step == 1) {
      click(Action::ImageZoom, "in");
      require(m_viewZoom > 1, "Draft image zoom uses the normal viewer controls");
      click(Action::ImageNext);
      require(draftImage(m_viewImage)->key == "2" && m_viewZoom == 1, "Gallery navigation resets image zoom");
    } else if (step == 2) {
      click(Action::ImagePrevious);
      onKey({.symbol = XKB_KEY_Left});
      require(draftImage(m_viewImage)->key == "10", "Draft image gallery wraps to the last image");
      onKey({.symbol = XKB_KEY_Escape});
      require(m_viewImage.empty(), "Escape returns to the draft without removing attachments");
      m_attachmentScroll = m_attachments.at(m_channelId).size() * layout::DraftAttachment::step;
    } else if (step == 3) {
      for (const auto& hit : m_hits)
        if (hit.action == Action::ViewAttachment)
          require(
              hit.x >= m_attachmentX && hit.x + hit.width <= m_attachmentX + m_attachmentWidth,
              "Scrolled attachment preview targets remain inside their viewport"
          );
      click(Action::RemoveAttachment, "10");
      require(m_viewImage.empty() && m_attachments.at(m_channelId).size() == 9, "Remove stays separate from preview");
    } else if (step == 4) {
      click(Action::ViewAttachment, "9");
      require(draftImage(m_viewImage)->key == "9", "A scrolled draft thumbnail opens the correct image");
      // A stale removal/send acknowledgement can arrive after opening the viewer.
      activate({.action = Action::RemoveAttachment, .id = "9"});
    } else if (step == 5) {
      require(m_viewImage.empty(), "Releasing the selected draft closes its viewer without a stale texture");
      require(m_attachments.at(m_channelId).size() == 8, "Previewing preserves the remaining draft images");
      activate({.action = Action::ViewAttachment, .id = "9"});
      require(m_viewImage.empty(), "A stale attachment target cannot open a removed draft image");
    } else {
      require(m_drafts.at(m_channelId) == "Unsent draft" && !m_sending, "Previewing never changes or sends the draft");
      require(m_pendingImages.empty(), "Local draft previews never request media from the network");
      std::clog << "Offline draft attachment replay passed\n";
    }
    m_window.invalidate();
  }
} // namespace app
