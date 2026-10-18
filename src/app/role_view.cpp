#include "app/client_application.h"
#include "app/theme.h"
#include "render/scene/text_node.h"

#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace app {
  float ClientApplication::roleLabel(
      Node& parent, float x, float y, float width, const std::string& text, float size, Color fallback,
      FontWeight weight, const std::string& user, const std::string& key, bool over,
      const std::optional<std::vector<std::string>>& roles
  ) {
    const auto colors =
        m_preferences.roleColors && !m_dmMode ? m_roleDirectory.colors(m_guildId, user, roles) : std::nullopt;
    std::optional<TextGradient> gradient;
    if (colors && colors->secondary) {
      gradient.emplace();
      gradient->colors = {
          rgbHex(colors->primary), rgbHex(*colors->secondary), rgbHex(colors->tertiary.value_or(*colors->secondary))
      };
      gradient->count = colors->tertiary ? 3 : 2;
      const bool animate = over
          && m_preferences.animateRoleColors
          && (m_window.active() || m_demo)
          && !settingsVisible()
          && !m_contextOpen
          && !m_emojiMenu
          && !m_gifMenu
          && m_dialogKind.empty()
          && m_viewImage.empty()
          && !m_videoExpanded;
      gradient->phase = m_roleMotion.phase(key, animate);
    }
    if (!colors)
      return label(parent, x, y, width, text, size, fallback, weight);
    const auto positions = TextEdit::stops(text);
    bool hasEmoji = false;
    for (std::size_t i = 1; i < positions.size(); ++i)
      hasEmoji |=
          m_emojiCatalog.isUnicode(std::string_view(text).substr(positions[i - 1], positions[i] - positions[i - 1]));
    if (!hasEmoji) {
      const float height = label(parent, x, y, width, text, size, colors ? rgbHex(colors->primary) : fallback, weight);
      if (gradient)
        static_cast<TextNode*>(parent.children().back().get())->setGradient(gradient);
      return height;
    }
    auto& renderer = m_window.target().renderer();
    auto displayed = text;
    // Ellipsize once, at grapheme boundaries, before separating text from emoji.
    if (renderer.measureText(displayed, size, weight).width > width) {
      std::size_t low = 0, high = positions.size() - 1;
      while (low < high) {
        const auto mid = (low + high + 1) / 2;
        if (renderer.measureText(text.substr(0, positions[mid]) + "…", size, weight).width <= width)
          low = mid;
        else
          high = mid - 1;
      }
      displayed = text.substr(0, positions[low]) + "…";
    }
    struct Fragment {
      std::string text;
      bool emoji;
      float width;
    };
    std::vector<Fragment> fragments;
    const auto stops = TextEdit::stops(displayed);
    for (std::size_t i = 1; i < stops.size(); ++i) {
      const auto part = displayed.substr(stops[i - 1], stops[i] - stops[i - 1]);
      const bool emoji = m_emojiCatalog.isUnicode(part);
      if (!fragments.empty() && fragments.back().emoji == emoji)
        fragments.back().text += part;
      else
        fragments.push_back({part, emoji, 0});
    }
    float fullWidth = 0;
    for (auto& fragment : fragments)
      fullWidth += fragment.width = renderer.measureText(fragment.text, size, weight).width;
    const auto metrics = renderer.measureText(displayed, size, weight);
    const float height = std::max(size + 4, metrics.bottom - metrics.top);
    auto row = std::make_unique<Node>();
    row->setPosition(x, y);
    row->setSize(width, height);
    row->setClipChildren(true);
    float offset = 0;
    for (const auto& fragment : fragments) {
      // All fragments share the full name's baseline, including color-font fallbacks.
      label(
          *row, offset, 0, fragment.width + 1, fragment.text, size,
          fragment.emoji ? theme::foreground : rgbHex(colors->primary), weight
      );
      auto& node = static_cast<TextNode&>(*row->children().back());
      node.setPosition(offset, -metrics.top);
      node.setMaxWidth(0);
      if (gradient && !fragment.emoji) {
        auto runGradient = *gradient;
        runGradient.offset = offset;
        runGradient.width = fullWidth;
        node.setGradient(runGradient);
      }
      offset += fragment.width;
    }
    parent.addChild(std::move(row));
    return height;
  }

  void ClientApplication::demoRoleStep() {
    using namespace theme;
    auto require = [](bool ok, const char* message) {
      if (!ok)
        throw std::runtime_error(message);
    };
    const auto step = m_demoInteractionStep++;
    int gradients = 0, moving = 0, plainNames = 0;
    auto inspect = [&](auto&& self, const Node& node) -> void {
      if (node.type() == NodeType::Text) {
        const auto& text = static_cast<const TextNode&>(node);
        if (text.gradient()) {
          ++gradients;
          if (text.gradient()->phase > 0)
            ++moving;
        }
        if (text.text() == "⪻𝚂ҟ₳𝘕ↁ✧亗⪼" && text.color() == foreground)
          ++plainNames;
      }
      for (const auto& child : node.children())
        self(self, *child);
    };
    inspect(inspect, *m_scene);
    if (m_demoOverlay == "role-emoji-cycle") {
      int emojiNames = 0, headingIcons = 0;
      const Node* memberList = nullptr;
      const auto check = [&](auto&& self, const Node& node) -> void {
        if (node.x() == m_window.width() - 240 && node.y() == 148)
          memberList = &node;
        if (node.type() == NodeType::Text) {
          const auto& text = static_cast<const TextNode&>(node);
          bool emoji = false;
          const auto stops = TextEdit::stops(text.text());
          for (std::size_t i = 1; i < stops.size(); ++i)
            emoji |=
                m_emojiCatalog.isUnicode(std::string_view(text.text()).substr(stops[i - 1], stops[i] - stops[i - 1]));
          if (emoji) {
            require(!text.gradient(), "Role gradients must never tint emoji graphemes");
            const bool nameRun = node.parent() && (node.parent()->x() == 50 || node.parent()->x() == 60);
            if (nameRun) {
              require(text.color() == foreground, "Username emoji must retain their native color-font rendering");
              if (text.text().find("🇩🇪") != std::string::npos
                  || text.text().find("🏳️‍🌈") != std::string::npos)
                ++emojiNames;
            }
          }
        }
        for (const auto& child : node.children())
          self(self, *child);
      };
      check(check, *m_scene);
      require(emojiNames == 4 && gradients >= 4, "Chat and member names retain flags alongside role gradients");
      require(memberList, "Role heading fixture must display the member list");
      float headingBaseline = 0, personBaseline = 0;
      for (const auto& child : memberList->children()) {
        if (child->type() == NodeType::Text) {
          const auto& text = static_cast<const TextNode&>(*child);
          if (text.text() == "🌙" || text.text() == "🔮") {
            require(child->x() == 18, "Member-list role icons belong on the left of headings");
            ++headingIcons;
          }
          if (text.text() == "Moonwarden — 1")
            headingBaseline = child->y();
        }
        if (child->x() == 60 && !child->children().empty())
          if (const auto* text = dynamic_cast<const TextNode*>(child->children().front().get());
              text && text->text().starts_with("Kitsune"))
            personBaseline = child->y() + text->y();
      }
      require(headingIcons == 2, "Each role heading gets one icon; individual member rows get none");
      require(
          headingBaseline > 0 && personBaseline > headingBaseline && personBaseline - headingBaseline < 36,
          "Role headings must sit closer to the first member without changing member row spacing"
      );
      const auto selected = std::ranges::find_if(m_hits, [&](const auto& hit) {
        return hit.action == Action::Guild && hit.id == m_guildId;
      });
      require(selected != m_hits.end(), "Active guild must be visible");
      for (const auto& child : m_scene->children())
        require(
            !(child->type() == NodeType::Rect
              && child->x() == selected->x
              && child->y() == selected->y
              && child->width() == 48
              && child->height() == 48),
            "Active guild icons must not have a selection background"
        );
      if (step == 0) {
        const auto hit = std::ranges::find_if(m_hits, [](const auto& h) {
          return h.action == Action::SelectMessage && h.id == "8200";
        });
        require(hit != m_hits.end(), "Emoji-name message must be visible");
        m_window.motion(hit->x + 12, hit->y + 5 + m_window.titlebarHeight() - 36);
      } else if (step == 1) {
        require(m_roleMotion.active(), "Text gradient animation must still run beside native emoji");
        m_window.motion(-100, -100);
      } else {
        require(!m_roleMotion.active(), "Leaving the username must restore static gradients");
        std::clog << "Offline role emoji and member heading replay passed\n";
      }
      m_window.invalidate();
      return;
    }
    if (m_demoOverlay == "role-members-cycle") {
      if (step == 0) {
        require(gradients == 0, "Uncached history authors start without invented role colors");
        m_roleDirectory.apply(
            api::parseRoleUpdates(
                "GUILD_MEMBERS_CHUNK",
                nlohmann::json{
                    {"guild_id", "1"},
                    {"members",
                     nlohmann::json::array(
                         {{{"user", {{"id", "11"}}}, {"roles", {"50"}}},
                          {{"user", {{"id", "12"}}}, {"roles", {"51"}}},
                          {{"user", {{"id", "13"}}}, {"roles", {"52"}}},
                          {{"user", {{"id", "14"}}}, {"roles", nlohmann::json::array()}}}
                     )}
                }
            )
        );
      } else {
        require(gradients == 4, "Targeted member data must recolor existing chat and member names on the next frame");
        require(!m_messages.front().memberRoles, "Resolving colors must not rewrite the original historical message");
        std::clog << "Offline member role replay passed\n";
      }
      m_window.invalidate();
      return;
    }
    auto hoverMessage = [&] {
      const auto at = std::ranges::find_if(m_hits, [](const auto& hit) {
        return hit.action == Action::SelectMessage && hit.id == "8200";
      });
      require(at != m_hits.end(), "Gradient demo message must be visible");
      m_window.motion(at->x + 12, at->y + 5 + m_window.titlebarHeight() - 36);
    };
    if (step == 0) {
      require(
          gradients == 4 && moving == 0 && !m_roleMotion.active(), "Idle gradients must be static in chat and members"
      );
      hoverMessage();
    } else if (step == 1) {
      require(m_roleMotion.active(), "Hovering a message must start compositor-paced role motion");
    } else if (step == 2) {
      require(moving == 1, "Only the hovered message name should animate");
      m_window.motion(-100, -100);
    } else if (step == 3) {
      require(!m_roleMotion.active() && moving == 0, "Leaving hover must stop frames and restore static gradients");
      activate({0, 0, 0, 0, Action::Preference, "animate_role_colors"});
      hoverMessage();
    } else if (step == 4) {
      require(
          gradients == 4 && moving == 0 && !m_roleMotion.active(),
          "Disabling animation retains static gradients on hover"
      );
      activate({0, 0, 0, 0, Action::Preference, "role_colors"});
    } else if (step == 5) {
      require(
          gradients == 0 && plainNames == 2 && !m_roleMotion.active(),
          "Disabling role colors restores normal text in both views"
      );
      activate({0, 0, 0, 0, Action::Preference, "role_colors"});
      activate({0, 0, 0, 0, Action::Preference, "animate_role_colors"});
      m_window.motion(-100, -100);
    } else if (step == 6) {
      require(gradients == 4 && moving == 0 && !m_roleMotion.active(), "Re-enabling colors restores the idle palettes");
      std::clog << "Offline role style replay passed\n";
    }
    m_window.invalidate();
  }
} // namespace app
