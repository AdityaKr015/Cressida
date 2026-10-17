#include "app/client_application.h"
#include "app/theme.h"
#include "render/scene/text_node.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace app {
  float ClientApplication::roleLabel(
      Node& parent, float x, float y, float width, const std::string& text, float size, Color fallback,
      FontWeight weight, const std::string& user, const std::string& key, bool over,
      const std::optional<std::vector<std::string>>& roles
  ) {
    const auto colors = m_preferences.roleColors && !m_dmMode ? m_roleDirectory.colors(m_guildId, user, roles) : std::nullopt;
    const float height = label(parent, x, y, width, text, size, colors ? rgbHex(colors->primary) : fallback, weight);
    if (colors && colors->secondary) {
      TextGradient gradient;
      gradient.colors = {rgbHex(colors->primary), rgbHex(*colors->secondary), rgbHex(colors->tertiary.value_or(*colors->secondary))};
      gradient.count = colors->tertiary ? 3 : 2;
      const bool animate = over && m_preferences.animateRoleColors && (m_window.active() || m_demo)
          && !m_settingsOpen && !m_contextOpen && !m_emojiMenu && !m_gifMenu && m_dialogKind.empty()
          && m_viewImage.empty() && !m_videoExpanded;
      gradient.phase = m_roleMotion.phase(key, animate);
      static_cast<TextNode*>(parent.children().back().get())->setGradient(gradient);
    }
    return height;
  }


  void ClientApplication::demoRoleStep() {
    using namespace theme;
    auto require = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
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
    auto hoverMessage = [&] {
      const auto at = std::ranges::find_if(m_hits, [](const auto& hit) {
        return hit.action == Action::SelectMessage && hit.id == "8200";
      });
      require(at != m_hits.end(), "Gradient demo message must be visible");
      m_window.motion(at->x + 12, at->y + 5 + m_window.titlebarHeight() - 36);
    };
    if (step == 0) {
      require(gradients == 4 && moving == 0 && !m_roleMotion.active(), "Idle gradients must be static in chat and members");
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
      require(gradients == 4 && moving == 0 && !m_roleMotion.active(), "Disabling animation retains static gradients on hover");
      activate({0, 0, 0, 0, Action::Preference, "role_colors"});
    } else if (step == 5) {
      require(gradients == 0 && plainNames == 2 && !m_roleMotion.active(), "Disabling role colors restores normal text in both views");
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
