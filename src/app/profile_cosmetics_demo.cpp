#include "app/client_application.h"
#include "app/theme.h"
#include "render/core/texture_manager.h"
#include "render/scene/image_node.h"
#include "render/scene/rect_node.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace app {
  namespace {
    constexpr std::string_view decorationPath = "/media/v1/collectibles-shop/33/static";
    constexpr std::string_view effectPath = "/assets/profile_effects/test/intro.png";
    constexpr std::string_view stillPath = "/assets/profile_effects/test/still.png";
    const std::pair<std::string_view, bool Preferences::*> cosmeticFlags[] = {
        {"avatar_decorations", &Preferences::showAvatarDecorations},
        {"nameplates", &Preferences::showNameplates},
        {"profile_effects", &Preferences::showProfileEffects},
        {"profile_frames", &Preferences::showProfileFrames},
        {"profile_banners", &Preferences::showProfileBanners},
        {"profile_themes", &Preferences::showProfileThemes}
    };
  } // namespace
  void ClientApplication::prepareCosmeticsDemo(const std::string& view) {
    using Json = nlohmann::json;
    prepareDmProfileDemo(view);
    m_demoOverlay = view;
    m_demoInteractionStep = 0;
    auto& profile = *m_dmProfile.data;
    profile.theme = std::array<std::uint32_t, 2>{0x624385, 0x32436B};
    profile.decoration = decorationPath;
    const auto product = api::parseCosmeticProduct(
        Json{
            {"sku_id", "44"},
            {"items",
             Json::array(
                 {{{"type", 1},
                   {"reducedMotionSrc", stillPath},
                   {"effects",
                    Json::array({{{"src", effectPath}, {"start", 0}, {"duration", 300000}, {"loop", true}}})}},
                  {{"type", 3},
                   {"inner_width", 320},
                   {"overflow_top", 8},
                   {"overflow_bottom", 8},
                   {"overflow_horizontal", 4},
                   {"layers",
                    Json::array(
                        {{{"id", "top"}, {"type", "staple"}, {"anchor", "top"}, {"order", "front"}},
                         {{"id", "bottom"}, {"type", "staple"}, {"anchor", "bottom"}, {"order", "front"}},
                         {{"id", "sides"}, {"type", "border"}, {"anchor", "center"}, {"order", "back"}}}
                    )}}}
             )}
        },
        "44"
    );
    profile.effect = product.effect;
    profile.frame = product.frame;
    m_userDirectory.consume(
        "USER_UPDATE",
        Json{
            {"id", "11"},
            {"avatar_decoration_data", {{"asset", "11111111111111111111111111111111"}, {"sku_id", "33"}}},
            {"collectibles", {{"nameplate", {{"sku_id", "22"}, {"palette", "violet"}}}}}
        }
    );
    m_userDirectory.consume(
        "USER_UPDATE",
        Json{{"id", m_account.id}, {"collectibles", {{"nameplate", {{"sku_id", "22"}, {"palette", "violet"}}}}}}
    );
    const int size = static_cast<int>(Style::space2Xl * 4);
    for (const auto& path :
         {std::string(decorationPath), api::collectibleAsset("33", {}, true), std::string(effectPath),
          std::string(stillPath), api::collectibleAsset("22"), api::collectibleAsset("22", {}, true),
          api::collectibleAsset("44", "top"), api::collectibleAsset("44", "bottom"),
          api::collectibleAsset("44", "sides")}) {
      std::vector<std::uint8_t> pixels(size * size * 4);
      for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
          const float radius = size / 2.0F;
          const float distance = std::hypot(x - radius, y - radius);
          const bool edge =
              x < Style::spaceXs || x >= size - Style::spaceXs || y < Style::spaceXs || y >= size - Style::spaceXs;
          const bool visible = path == decorationPath   ? distance < radius && distance > radius - Style::spaceXs
              : path.contains("/collectibles-shop/22/") ? true
                                                        : edge;
          const auto at = (y * size + x) * 4;
          pixels[at] = 153;
          pixels[at + 1] = 117;
          pixels[at + 2] = 221;
          pixels[at + 3] = visible ? 160 : 0;
        }
      auto& cached = m_images[path];
      cached.texture = m_window.context().textureManager().loadFromRgba(pixels.data(), size, size);
      if (path.ends_with("/animated")) {
        auto animation = std::make_shared<media::Animation>();
        animation->width = animation->height = size;
        animation->duration = std::chrono::milliseconds(Style::animNormal);
        for (int i = 0; i < 2; ++i) {
          media::Frame frame;
          frame.rgba = pixels;
          frame.duration = animation->duration / 2;
          for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 4)
            frame.rgba[pixel] = i ? 221 : 153;
          animation->frames.push_back(std::move(frame));
        }
        cached.animation = std::move(animation);
      }
    }
    m_pointerX = m_pointerY = -1000;
    if (view.contains("-hover"))
      m_preferences.showProfileEffects = false;
  }
  void ClientApplication::demoCosmeticsStep() {
    if (m_demoOverlay.contains("-hover")) {
      demoCosmeticsHoverStep();
      return;
    }
    const auto require = [](bool condition, const char* message) {
      if (!condition)
        throw std::runtime_error(message);
    };
    const auto images = [&](const std::string& path) {
      std::size_t count = 0;
      const auto at = m_images.find(path);
      if (at == m_images.end())
        return count;
      const auto inspect = [&](auto&& self, const Node& node) -> void {
        if (node.type() == NodeType::Image && static_cast<const ImageNode&>(node).textureId() == at->second.texture.id)
          ++count;
        for (const auto& child : node.children())
          self(self, *child);
      };
      inspect(inspect, *m_scene);
      return count;
    };
    const auto colors = [&] {
      const auto still = m_images.at(api::collectibleAsset("22")).texture.id;
      const auto moving = m_images.at(api::collectibleAsset("22", {}, true)).texture.id;
      const auto expected = rgbHex(*api::nameplateBackground("violet", theme::lightTheme));
      bool palette = false, artwork = false, accountPalette = false;
      const auto inspect = [&](auto&& self, const Node& node) -> void {
        if (node.type() == NodeType::Rect) {
          const auto& style = static_cast<const RectNode&>(node).style();
          if (style.fillMode == FillMode::LinearGradient
              && style.gradientStops.back().color.r == expected.r
              && style.gradientStops.back().color.g == expected.g
              && style.gradientStops.back().color.b == expected.b) {
            palette = true;
            accountPalette |= style.gradientStops.front().color.a == Style::nameplateAccountStartOpacity;
          }
        }
        if (node.type() == NodeType::Image) {
          const auto& image = static_cast<const ImageNode&>(node);
          if (image.textureId() == still || image.textureId() == moving) {
            require(image.tint() == Color{1, 1, 1, 1}, "Nameplate images retain their original RGB channels");
            for (const auto* ancestor = &node; ancestor; ancestor = ancestor->parent())
              require(ancestor->opacity() == 1, "Nameplate colors are not dimmed by an extra opacity layer");
            artwork = true;
          }
        }
        for (const auto& child : node.children())
          self(self, *child);
      };
      inspect(inspect, *m_scene);
      require(
          palette && artwork && accountPalette,
          "Nameplate palette and undimmed artwork render together in the current color mode"
      );
    };
    const auto effectClipping = [&] {
      const auto moving = m_images.at(std::string(effectPath)).texture.id;
      const auto still = m_images.at(std::string(stillPath)).texture.id;
      std::size_t found = 0;
      const auto inspect = [&](auto&& self, const Node& node) -> void {
        if (node.type() == NodeType::Image) {
          const auto& image = static_cast<const ImageNode&>(node);
          if (image.textureId() == moving || image.textureId() == still) {
            const auto& clip = image.roundedClip();
            require(
                clip.enabled
                    && clip.radius > 0
                    && clip.x == -image.x()
                    && clip.y == -image.y()
                    && clip.width == image.parent()->width()
                    && clip.height == image.parent()->height(),
                "Moving and reduced-motion effect layers share the profile's rounded clip"
            );
            ++found;
          }
        }
        for (const auto& child : node.children())
          self(self, *child);
      };
      inspect(inspect, *m_scene);
      require(found > 0, "Equipped effects render with a rounded profile mask");
    };
    const auto framed = [&] { return images(api::collectibleAsset("44", "top")); };
    const auto frameClipping = [&] {
      const auto border = m_images.at(api::collectibleAsset("44", "sides")).texture.id;
      std::size_t found = 0;
      const auto inspect = [&](auto&& self, const Node& node) -> void {
        if (node.type() == NodeType::Image && static_cast<const ImageNode&>(node).textureId() == border) {
          const auto* clip = node.parent();
          const auto* outer = clip ? clip->parent() : nullptr;
          require(
              clip && outer && clip->clipChildren() && clip->y() > 0 && clip->y() + clip->height() < outer->height(),
              "Tiled frame borders cannot protrude into either staple overflow region"
          );
          ++found;
        }
        for (const auto& child : node.children())
          self(self, *child);
      };
      inspect(inspect, *m_scene);
      require(found > 0, "Equipped frames include a clipped border layer");
    };
    const auto nameplate = [&] {
      return images(api::collectibleAsset("22")) + images(api::collectibleAsset("22", {}, true));
    };
    const auto click = [&](Action action) {
      const auto at = std::ranges::find(m_hits, action, &Hit::action);
      require(at != m_hits.end(), "Cosmetic layers preserve visible profile actions");
      const auto x = at->x + at->width / 2;
      const auto y = at->y + at->height / 2 + m_window.titlebarHeight() - layout::Window::titlebarHeight;
      m_window.motion(x, y);
      m_window.click(x, y, 1);
      m_window.released();
    };
    const auto step = m_demoInteractionStep++;
    if (step == 0) {
      frameClipping();
      effectClipping();
      colors();
      require(
          images(std::string(decorationPath)) >= 3 && nameplate() && images(std::string(effectPath)) && framed(),
          "Equipped cosmetics render in messages, conversation rows and the DM profile"
      );
      extendedPreference("avatar_decorations");
    } else if (step == 1) {
      require(
          !images(std::string(decorationPath)) && nameplate() && framed(), "Avatar decorations toggle independently"
      );
      extendedPreference("nameplates");
    } else if (step == 2) {
      require(!nameplate() && images(std::string(effectPath)) && framed(), "Nameplates toggle independently");
      extendedPreference("profile_effects");
    } else if (step == 3) {
      require(!images(std::string(effectPath)) && framed(), "Profile effects toggle independently");
      extendedPreference("profile_frames");
    } else if (step == 4) {
      require(!framed() && images(m_dmProfile.data->banner), "Profile frames preserve banners and profile content");
      extendedPreference("profile_banners");
    } else if (step == 5) {
      require(!images(m_dmProfile.data->banner), "Profile banners toggle independently");
      extendedPreference("profile_themes");
    } else if (step == 6) {
      for (const auto& [key, field] : cosmeticFlags)
        require(!(m_preferences.*field), "All cosmetic categories can be disabled");
      openSettings();
      selectSettingsPage("cosmetics");
    } else if (step == 7) {
      for (const auto& [key, field] : cosmeticFlags) {
        const auto widget = m_widgets.find("toggle:" + std::string(key));
        require(widget != m_widgets.end(), "Each cosmetic category has a separate native toggle");
        float left = 0, top = 0, right = 0, bottom = 0;
        Node::transformedBounds(widget->second, left, top, right, bottom);
        const auto x = (left + right) / 2, y = (top + bottom) / 2;
        m_window.motion(x, y);
        m_window.click(x, y, 1);
        m_window.released();
        require(m_preferences.*field, "The actual native cosmetic toggle changes its matching preference");
      }
      activate({.action = Action::CloseSettings});
    } else if (step == 8) {
      require(
          images(std::string(decorationPath)) && nameplate() && images(std::string(effectPath)) && framed(),
          "Cosmetics restore immediately after changing settings"
      );
      showProfile("11");
    } else if (step == 9) {
      frameClipping();
      effectClipping();
      require(m_dialogBounds.width <= layout::Profile::width, "Framed profile popups use compact card geometry");
      require(
          m_dialogKind == "profile" && images(std::string(effectPath)) && framed(),
          "Full profile cards reuse the same equipped effects and frame layers"
      );
      click(Action::CloseDialog);
      require(m_dialogKind.empty(), "Decorative layers do not intercept profile close clicks");
      m_preferences.reducedMotion = true;
    } else if (step == 10) {
      effectClipping();
      require(
          images(std::string(stillPath)) && !images(std::string(effectPath)),
          "Reduced motion uses the effect’s static asset"
      );
      m_dmProfile.data->privateProfile = true;
    } else if (step == 11) {
      require(!framed() && !images(std::string(stillPath)), "Private profiles do not render effect or frame data");
      m_userDirectory.consume(
          "USER_UPDATE", nlohmann::json{{"id", "11"}, {"avatar_decoration_data", nullptr}, {"collectibles", nullptr}}
      );
      m_userDirectory.consume("USER_UPDATE", nlohmann::json{{"id", m_account.id}, {"collectibles", nullptr}});
      m_dmProfile.data->decoration.clear();
    } else if (step == 12) {
      require(
          !nameplate() && !images(std::string(decorationPath)),
          "Live clears remove cosmetics from all visible user surfaces"
      );
      require(m_drafts["9001"] == "Keep this draft", "Cosmetic controls preserve chat drafts");
      std::clog << "Offline cosmetics, independent native toggles, profile layers and reduced motion replay passed\n";
    }
    m_window.invalidate();
  }
  void ClientApplication::demoCosmeticsHoverStep() {
    const auto require = [](bool ok, const char* reason) {
      if (!ok)
        throw std::runtime_error(reason);
    };
    const auto playing = [&](const std::string& sku) {
      return m_images.at(api::collectibleAsset(sku, {}, true)).playingDraw == m_animationDraw;
    };
    const auto stillDecorations = [&] {
      std::size_t count = 0;
      const auto texture = m_images.at(std::string(decorationPath)).texture.id;
      const auto inspect = [&](auto&& self, const Node& node) -> void {
        if (node.type() == NodeType::Image && static_cast<const ImageNode&>(node).textureId() == texture)
          ++count;
        for (const auto& child : node.children())
          self(self, *child);
      };
      inspect(inspect, *m_scene);
      return count;
    };
    const auto point = [&](float x, float y) {
      m_window.motion(x, y + m_window.titlebarHeight() - layout::Window::titlebarHeight);
    };
    const auto row = [&] {
      const auto hit = std::ranges::find_if(m_hits, [](const Hit& hit) {
        return hit.action == Action::Channel && hit.id == "9001";
      });
      if (hit == m_hits.end())
        throw std::runtime_error(
            "Hover replay has no conversation row at step " + std::to_string(m_demoInteractionStep)
        );
      point(hit->x + hit->width / 2, hit->y + hit->height / 2);
    };
    const auto step = m_demoInteractionStep++;
    if (step == 0) {
      require(!playing("22") && !playing("33"), "Idle cosmetics do not animate");
      row();
    } else if (step == 1) {
      require(
          playing("22") && playing("33") && stillDecorations() >= 2,
          "Only the hovered row animates while its other avatar copies keep independent still textures"
      );
      const auto hit = std::ranges::find(m_hits, Action::SelectMessage, &Hit::action);
      require(hit != m_hits.end(), "Hover replay has visible message text");
      point(hit->x + hit->width / 2, hit->y + hit->height / 2);
    } else if (step == 2) {
      require(!playing("22") && playing("33"), "Hovering message text animates only its user's decoration");
      point(m_window.width() - memberPaneWidth() / 2, layout::Members::top + Style::spaceSection);
    } else if (step == 3) {
      require(!playing("22") && playing("33"), "Hovering the profile animates its decoration");
      point(-1000, -1000);
    } else if (step == 4) {
      require(!playing("22") && !playing("33"), "Leaving a user returns both cosmetics to still artwork");
      m_preferences.reducedMotion = true;
      row();
    } else if (step == 5) {
      require(!playing("22") && !playing("33"), "Reduced motion overrides hovered cosmetics");
      m_preferences.reducedMotion = false;
      m_preferences.animateAvatars = false;
    } else if (step == 6) {
      require(!playing("22") && !playing("33"), "Animate avatars also controls hovered cosmetic motion");
      m_preferences.animateAvatars = true;
      m_preferences.showAvatarDecorations = m_preferences.showNameplates = false;
    } else if (step == 7) {
      require(!playing("22") && !playing("33"), "Hidden cosmetic categories do not animate");
      m_preferences.showAvatarDecorations = m_preferences.showNameplates = true;
      openSettings();
    } else if (step == 8) {
      require(!playing("22") && !playing("33"), "Settings block animation of covered rows");
      activate({.action = Action::CloseSettings});
    } else if (step == 9) {
      require(playing("22") && playing("33"), "Closing settings restores hover animation");
      point(-1000, -1000);
      showProfile("11");
    } else if (step == 10) {
      require(
          !playing("22") && playing("33") && stillDecorations() >= 3,
          "An open popup animates its avatar without hovering while covered user copies stay still"
      );
      point(m_dialogBounds.x + m_dialogBounds.width / 2, m_dialogBounds.y + m_dialogBounds.height / 2);
    } else if (step == 11) {
      require(!playing("22") && playing("33"), "Hovering a popup animates its decoration without covered nameplates");
      m_preferences.reducedMotion = true;
      point(-1000, -1000);
    } else if (step == 12) {
      require(
          !playing("22") && !playing("33") && m_drafts["9001"] == "Keep this draft",
          "Reduced motion stops an open profile decoration and preserves the draft"
      );
      activate({.action = Action::CloseDialog});
      std::clog << "Offline hover cosmetics, open profile animation and motion preferences replay passed\n";
    }
    m_window.invalidate();
  }
} // namespace app
