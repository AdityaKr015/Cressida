#pragma once

#include "render/core/color.h"
#include "ui/style.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <string>
#include <string_view>

namespace app {
  inline std::array<Color, 4> logoGradientColors(Color accent, Color foreground, Color background, bool light) {
    const auto shadowTarget = light ? foreground : background;
    const auto shadow = lerpColor(accent, shadowTarget, Style::logoShadowMix);
    return {
        lerpColor(accent, light ? background : foreground, Style::logoHighlightMix), accent, shadow,
        lerpColor(shadow, shadowTarget, Style::logoHighlightMix)
    };
  }
  inline std::string logoColorHex(Color color) {
    const auto byte = [](float value) {
      return static_cast<unsigned>(
          std::lround(std::clamp(value, 0.0F, 1.0F) * std::numeric_limits<std::uint8_t>::max())
      );
    };
    return std::format("#{:02x}{:02x}{:02x}", byte(color.r), byte(color.g), byte(color.b));
  }
  inline std::string logoGradientKey(const std::array<Color, 4>& colors) {
    std::string result;
    for (const auto color : colors)
      result += logoColorHex(color);
    return result;
  }
  inline std::string themedLogoSvg(std::string_view source, const std::array<Color, 4>& colors) {
    // These are the four stop colors in the bundled artwork. Keep every path,
    // gradient transform and layer relationship intact; only replace stop colors.
    constexpr std::array<std::string_view, 4> original{"#c8b2fc", "#b79af1", "#4d29a8", "#3c1f96"};
    std::array<std::string, original.size()> replacement;
    for (std::size_t i = 0; i < colors.size(); ++i)
      replacement[i] = logoColorHex(colors[i]);
    std::string result;
    result.reserve(source.size());
    for (std::size_t at = 0; at < source.size();) {
      const auto match = std::ranges::find(original, source.substr(at, original.front().size()));
      if (source[at] == '#' && match != original.end()) {
        result += replacement[match - original.begin()];
        at += match->size();
      } else
        result += source[at++];
    }
    return result;
  }
} // namespace app
