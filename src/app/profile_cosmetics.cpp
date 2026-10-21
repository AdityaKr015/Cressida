#include "app/client_application.h"
#include "app/theme.h"
#include "render/scene/image_node.h"
#include "render/scene/rect_node.h"

#include <algorithm>
#include <cmath>

namespace app {
  bool ClientApplication::cosmeticHovered(const Node& parent, const layout::Frame& region) const {
    if ((!m_demo && !m_window.active())
        || !m_viewImage.empty()
        || m_videoExpanded
        || m_contextOpen
        || settingsVisible()
        || m_selectRequest
        || m_emojiMenu
        || m_gifMenu
        || m_presenceMenu
        || userAccountDialogOpen()
        || m_resizingSidebar
        || (m_railDrag && m_railDrag->active))
      return false;
    float x = region.x, y = region.y;
    for (const auto* node = &parent; node && node != m_scene.get(); node = node->parent()) {
      x += node->x();
      y += node->y();
    }
    if (!m_dialogKind.empty()) {
      if (m_dialogKind != "profile"
          || x != m_dialogBounds.x
          || y != m_dialogBounds.y
          || region.width != m_dialogBounds.width
          || region.height != m_dialogBounds.height)
        return false;
      return true;
    }
    if (!hovered(x, y, region.width, region.height))
      return false;
    for (const auto* node = &parent; node && node != m_scene.get(); node = node->parent()) {
      if (!node->clipChildren())
        continue;
      float nx = 0, ny = 0;
      for (const auto* ancestor = node; ancestor && ancestor != m_scene.get(); ancestor = ancestor->parent()) {
        nx += ancestor->x();
        ny += ancestor->y();
      }
      if (!hovered(nx, ny, node->width(), node->height()))
        return false;
    }
    return true;
  }
  void ClientApplication::drawAvatarDecoration(
      Node& parent, float x, float y, float size, const std::string& path, const layout::Frame& hoverRegion
  ) {
    if (!m_preferences.showAvatarDecorations || path.empty())
      return;
    const auto frame = layout::Cosmetics::avatarDecoration(x, y, size);
    auto node = std::make_unique<Node>();
    node->setHitTestVisible(false);
    auto* art = parent.addChild(std::move(node));
    const bool play =
        !m_preferences.reducedMotion && m_preferences.animateAvatars && cosmeticHovered(parent, hoverRegion);
    const auto still = api::cosmeticAssetVariant(path, false), moving = api::cosmeticAssetVariant(path, true);
    if (!picture(
            *art, frame.x, frame.y, frame.width, frame.height, play ? moving : still, 0, false, false,
            ImageFitMode::Contain
        )
        && play)
      picture(*art, frame.x, frame.y, frame.width, frame.height, still, 0, false, false, ImageFitMode::Contain);
  }
  void ClientApplication::drawNameplate(
      Node& parent, const layout::Frame& frame, const std::string& user, const std::string& guild,
      NameplatePlacement placement, bool selected
  ) {
    if (!m_preferences.showNameplates)
      return;
    const auto cosmetics = m_userDirectory.cosmetics(user, guild);
    auto path = cosmetics.nameplate.value_or("");
    if (path.empty())
      return;
    const bool over = cosmeticHovered(parent, frame);
    if (const auto background = api::nameplateBackground(cosmetics.nameplatePalette.value_or(""), theme::lightTheme)) {
      const auto color = rgbHex(*background);
      const auto stop = [&](float position, float alpha) {
        auto c = color;
        c.a = alpha;
        return GradientStop{position, c};
      };
      RoundedRectStyle style;
      style.fillMode = FillMode::LinearGradient;
      style.radius = Style::scaledShapeRadius(Style::surfaceRadius, frame.width, frame.height);
      if (placement == NameplatePlacement::Account) {
        const auto end =
            stop(1, theme::lightTheme ? Style::nameplateAccountLightEndOpacity : Style::nameplateAccountDarkEndOpacity);
        style.gradientStops = {stop(0, Style::nameplateAccountStartOpacity), end, end, end};
      } else {
        const float alpha = selected                          ? Style::nameplateRowSelectedEndOpacity
            : over && placement == NameplatePlacement::Member ? Style::nameplateRowHoverEndOpacity
                                                              : Style::nameplateRowEndOpacity;
        style.gradientStops = {
            stop(0, 0), stop(Style::nameplateGradientStart, Style::nameplateRowMiddleOpacity),
            stop(Style::nameplateGradientMiddle, Style::nameplateRowMiddleOpacity), stop(1, alpha)
        };
      }
      auto backdrop = std::make_unique<RectNode>();
      backdrop->setPosition(frame.x, frame.y);
      backdrop->setSize(frame.width, frame.height);
      backdrop->setStyle(style);
      backdrop->setHitTestVisible(false);
      parent.addChild(std::move(backdrop));
    }
    path = api::cosmeticAssetVariant(path, !m_preferences.reducedMotion && m_preferences.animateAvatars && over);
    auto node = std::make_unique<Node>();
    node->setHitTestVisible(false);
    auto* art = parent.addChild(std::move(node));
    if (!picture(*art, frame.x, frame.y, frame.width, frame.height, path, Style::surfaceRadius, false)) {
      const auto still = m_userDirectory.cosmetics(user, guild).nameplate.value_or("");
      if (path != still)
        picture(*art, frame.x, frame.y, frame.width, frame.height, still, Style::surfaceRadius, false);
    }
  }
  void ClientApplication::drawProfileCosmetics(
      Node& parent, const layout::Frame& bounds, const api::UserProfile& profile, bool foreground, float radius
  ) {
    if (bounds.width <= 0 || bounds.height <= 0 || profile.privateProfile)
      return;
    if (m_preferences.showProfileFrames && profile.frame) {
      const auto& frame = *profile.frame;
      if (frame.innerWidth > 0) {
        const float scale = bounds.width / frame.innerWidth;
        const float overflow = frame.overflowHorizontal * scale;
        const auto body = layout::Cosmetics::profileFrameBody(bounds, overflow);
        const auto outer =
            layout::Cosmetics::profileFrameOuter(body, frame.overflowTop * scale, frame.overflowBottom * scale);
        const float left = outer.x, top = outer.y, width = outer.width, height = outer.height;
        auto clip = std::make_unique<Node>();
        clip->setPosition(left, top);
        clip->setSize(width, height);
        clip->setClipChildren(true);
        clip->setHitTestVisible(false);
        auto* art = parent.addChild(std::move(clip));
        for (const auto& layer : frame.layers) {
          if ((layer.order == "front") != foreground)
            continue;
          const auto cached = m_images.find(layer.source);
          const float imageHeight = cached != m_images.end() && cached->second.texture.valid()
              ? width * cached->second.texture.height / cached->second.texture.width
              : width;
          if (layer.type == "border" || layer.type == "rail") {
            if (layer.type == "rail" && layer.responsive && !layout::Cosmetics::responsiveFrameRail(bounds))
              continue;
            auto bodyClip = std::make_unique<Node>();
            bodyClip->setPosition(0, body.y - top);
            bodyClip->setSize(width, body.height);
            bodyClip->setClipChildren(true);
            bodyClip->setHitTestVisible(false);
            auto* sides = art->addChild(std::move(bodyClip));
            if (layer.type == "border") {
              constexpr std::size_t maximumBorderTiles = 128;
              for (std::size_t tile = 0;
                   tile < maximumBorderTiles && imageHeight > 0 && tile * imageHeight < body.height; ++tile)
                picture(
                    *sides, 0, tile * imageHeight, width, imageHeight, layer.source, 0, false, false,
                    ImageFitMode::Stretch
                );
            } else {
              const float y = layer.anchor == "bottom" ? body.height - imageHeight
                  : layer.anchor == "center"           ? (body.height - imageHeight) / 2
                                                       : 0;
              picture(*sides, 0, y, width, imageHeight, layer.source, 0, false, false, ImageFitMode::Stretch);
            }
          } else {
            const float y = layer.anchor == "bottom" ? height - imageHeight
                : layer.anchor == "center"           ? (height - imageHeight) / 2
                                                     : 0;
            picture(*art, 0, y, width, imageHeight, layer.source, 0, false, false, ImageFitMode::Stretch);
          }
        }
      }
    }
    if (!foreground || !m_preferences.showProfileEffects || !profile.effect)
      return;
    const auto& effect = *profile.effect;
    auto clip = std::make_unique<Node>();
    clip->setPosition(bounds.x, bounds.y);
    clip->setSize(bounds.width, bounds.height);
    clip->setClipChildren(true);
    clip->setHitTestVisible(false);
    auto* art = parent.addChild(std::move(clip));
    const auto maskImage = [&] {
      auto& image = static_cast<ImageNode&>(*art->children().back());
      image.setRoundedClip(
          {-image.x(), -image.y(), bounds.width, bounds.height,
           Style::scaledShapeRadius(radius, bounds.width, bounds.height), true}
      );
    };
    if (m_preferences.reducedMotion || !m_preferences.animateAvatars || (!m_demo && !m_window.active())) {
      if (!effect.still.empty() && picture(*art, 0, 0, bounds.width, bounds.height, effect.still, 0, false))
        maskImage();
      return;
    }
    constexpr std::size_t maximumCosmeticClocks = 64;
    if (m_cosmeticClocks.size() >= maximumCosmeticClocks)
      m_cosmeticClocks.clear();
    const auto key = profile.user.id + ":" + effect.sku + ":" + (m_dialogKind.empty() ? "panel" : "card");
    auto& clock = m_cosmeticClocks[key];
    const auto now = std::chrono::steady_clock::now();
    if (clock.frame + 1 < m_animationDraw || clock.start == std::chrono::steady_clock::time_point{})
      clock.start = now;
    clock.frame = m_animationDraw;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - clock.start);
    for (const auto& layer : effect.layers) {
      const auto sample = api::sampleProfileEffect(layer, elapsed);
      m_animationDelay = std::min(m_animationDelay, sample.next);
      if (!sample.visible)
        continue;
      const auto cached = m_images.find(layer.source);
      const float scale =
          cached != m_images.end() && cached->second.texture.valid() ? bounds.width / cached->second.texture.width : 1;
      const float height = cached != m_images.end() && cached->second.texture.valid()
          ? cached->second.texture.height * scale
          : bounds.height;
      if (picture(
              *art, layer.x * scale, layer.y * scale, bounds.width, height, layer.source, 0, false, false,
              ImageFitMode::Stretch, sample.elapsed
          ))
        maskImage();
    }
  }
} // namespace app
