#include "app/layout_metrics.h"
#include "app/window_controls.h"

#include <iostream>
#include <stdexcept>

int main() {
  const auto require = [](bool ok, const char* reason) {
    if (!ok)
      throw std::runtime_error(reason);
  };
  try {
    using B = app::WindowButton;
    app::WindowCapabilities capabilities;
    app::WindowControlPreferences preference;
    require(
        app::visibleWindowButtons(preference, capabilities) == std::vector{B::Minimize, B::Maximize, B::Close},
        "Older compositors retain the conventional actions"
    );
    const std::array<std::uint32_t, 3> umbriel{1, 2, 3};
    capabilities.configure(umbriel);
    require(capabilities.minimize(), "Capabilities wait for the matching surface configure");
    capabilities.surfaceConfigured();
    require(
        app::visibleWindowButtons(preference, capabilities) == std::vector{B::Maximize, B::Close},
        "Umbriel's absent minimize capability hides only minimize"
    );
    const std::array<std::uint32_t, 2> supported{2, 4};
    capabilities.configure(supported);
    require(!capabilities.minimize(), "A pending update cannot change displayed controls early");
    capabilities.surfaceConfigured();
    require(capabilities.minimize(), "Runtime capability changes restore supported actions");
    for (int order = 0; order < app::windowButtonOrderCount; ++order) {
      preference.order = order;
      for (unsigned hidden = 0; hidden < 8; ++hidden) {
        preference.minimize = !(hidden & 1);
        preference.maximize = !(hidden & 2);
        preference.close = !(hidden & 4);
        const auto buttons = app::visibleWindowButtons(preference, capabilities);
        for (const bool left : {false, true}) {
          const auto width = float(Style::windowMinimumWidth);
          const auto area = app::layout::Titlebar::buttons(width, buttons.size(), left);
          require(area.x >= 0 && area.x + area.width <= width, "Button group stays within the titlebar");
          for (std::size_t i = 0; i < buttons.size(); ++i) {
            const auto frame = app::layout::Titlebar::button(width, buttons.size(), left, i);
            require(
                !app::layout::Titlebar::dragContains(
                    frame.x + frame.width / 2, frame.height / 2, width, buttons.size(), left
                ),
                "Every visible button is excluded from window dragging on both sides"
            );
          }
          require(
              app::layout::Titlebar::dragContains(
                  width / 2, app::layout::Titlebar::height / 2, width, buttons.size(), left
              ),
              "Titlebar body remains draggable for every visibility/order combination"
          );
          if (buttons.empty())
            require(
                app::layout::Titlebar::dragContains(0, 0, width, 0, left)
                    && app::layout::Titlebar::dragContains(width - 1, 0, width, 0, left),
                "Hiding every button releases the entire titlebar to dragging"
            );
        }
      }
    }
    preference = {};
    preference.order = 2;
    require(
        app::visibleWindowButtons(preference, capabilities) == std::vector{B::Close, B::Maximize, B::Minimize},
        "Button ordering is retained independently of placement"
    );
    capabilities.configure({});
    capabilities.surfaceConfigured();
    require(
        app::visibleWindowButtons(preference, capabilities) == std::vector{B::Close},
        "An empty capability set retains close while removing unsupported actions"
    );
    require(
        app::windowButtonIcon(B::Maximize, true) == app::WindowIcon::Restore
            && app::windowButtonIcon(B::Maximize, false) == app::WindowIcon::Maximize,
        "Maximize uses the distinct restore icon in maximized windows"
    );
    std::cout << "Window capability, button and drag geometry checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
