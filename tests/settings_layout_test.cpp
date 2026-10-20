#include "app/layout_metrics.h"

#include <iostream>
#include <stdexcept>

int main() {
  const auto require = [](bool ok, const char* reason) {
    if (!ok)
      throw std::runtime_error(reason);
  };
  try {
    using S = app::layout::Settings;
    for (const float width : {480.0F, 622.0F, 760.0F, 1120.0F, 1920.0F}) {
      const auto frame = app::layout::settingsFrame(width);
      const auto navRight = frame.x + frame.navigationContentWidth;
      const auto cardRight = frame.contentLeft + frame.contentWidth;
      const auto navHitLeft = frame.navigationScrollbarX - S::scrollbarInwardExtent;
      const auto bodyHitLeft = frame.contentScrollbarX - S::scrollbarInwardExtent;
      require(
          navHitLeft - navRight >= Style::scrollbarGap,
          "Expanded navigation scrollbar and pointer slop stay outside the clipped tab column"
      );
      require(
          bodyHitLeft - cardRight >= Style::scrollbarGap,
          "Expanded body scrollbar and pointer slop stay outside settings category backgrounds"
      );
      require(
          frame.contentX + frame.innerWidth + S::contentInset == cardRight,
          "Fields, measurement, category padding and viewport widths share the reduced content frame"
      );
      require(
          frame.innerWidth > 0 && frame.navigationContentWidth > 0,
          "Reserved gutters retain usable navigation and settings content at narrow widths"
      );
      require(
          frame.contentLeft - (frame.x + frame.navigationWidth) == S::contentGutter,
          "Reserving scrollbar lanes preserves the navigation-to-content gutter"
      );
      require(
          frame.navigationScrollbarX + S::scrollbarWidth == frame.x + frame.navigationWidth,
          "The navigation scrollbar stays inside its owning column"
      );
      // Neither hidden bars nor hover/drag expansion alter the content frame.
      for (const float thickness : {Style::scrollbarWidth, Style::scrollbarHoverWidth}) {
        const auto inward = thickness - Style::scrollbarWidth + Style::scrollbarHitSlop;
        require(
            frame.navigationScrollbarX - inward > navRight && frame.contentScrollbarX - inward > cardRight,
            "Both resting and expanded scrollbar hit regions remain separate from content"
        );
      }
    }
    std::cout << "Settings scrollbar lane geometry passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
