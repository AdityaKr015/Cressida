#include "app/client_application.h"

#include <algorithm>

namespace app {
  bool ClientApplication::hovered(float x, float y, float width, float height) const {
    return m_pointerX >= x && m_pointerY >= y && m_pointerX < x + width && m_pointerY < y + height;
  }
  void ClientApplication::updateCursor() {
    bool interactive = false;
    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it)
      if (it->contains(m_pointerX, m_pointerY)) {
        interactive =
            it->action != Action::Composer && it->action != Action::SelectMessage && it->action != Action::None;
        break;
      }
    if (m_viewImage.empty()
        && !m_videoExpanded
        && !m_presenceMenu
        && m_controlInput.inputAreaAt(m_pointerX, m_pointerY + m_window.titlebarHeight() - 36))
      interactive = true;
    m_window.setPointerCursor(interactive);
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
    std::erase_if(m_hits, [](const auto& hit) {
      return hit.action != Action::Close && hit.action != Action::Minimize && hit.action != Action::Maximize;
    });
    Color backdrop = rgbHex(0x08090D);
    backdrop.a = 0.96F;
    rectangle(root, 0, 36, w, h - 36, backdrop);
    m_hits.push_back({0, 36, w, h - 36, Action::CloseImage, {}});
    const float left = 60, top = 94, availableW = w - 120, availableH = h - 174;
    const auto found = m_images.find(m_viewImage);
    const auto texture = found == m_images.end() ? TextureHandle{} : found->second.texture;
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
    picture(*imageLayer, x, y, iw, ih, m_viewImage, 8, true);
    root.addChild(std::move(imageLayer));
    const float hitX = std::max(left, left + x), hitY = std::max(top, top + y);
    const float hitRight = std::min(left + availableW, left + x + iw),
                hitBottom = std::min(top + availableH, top + y + ih);
    m_hits.push_back(
        {hitX, hitY, std::max(0.0F, hitRight - hitX), std::max(0.0F, hitBottom - hitY), Action::ImagePan, {}}
    );
    label(root, 28, 53, w - 140, "Image", 17, rgbHex(0xEEEFF5), FontWeight::SemiBold);
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
    button(root, {w - 182, h - 62, 156, 34, Action::OpenUrl, m_viewUrl}, "Open original");
  }
} // namespace app
