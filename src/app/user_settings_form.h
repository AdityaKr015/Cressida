#pragma once

#include "render/scene/node.h"
#include "ui/controls/button.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace app {
  struct UserSettingRow {
    float top = 0, height = 0;
  };
  // All callbacks compose retained controls and measured rows on the UI thread.
  struct UserSettingsForm {
    Node& node;
    float& y;
    float width;
    std::function<void(const std::string&)> section;
    std::function<UserSettingRow(const std::string&, const std::string&, float, bool)> rowControl;
    std::function<void(Node&, float, float, float, const std::string&, const std::string&, ButtonVariant, bool)>
        buttonControl;
    std::function<void(
        const std::string&, const std::string&, const std::string&, const std::string&,
        std::function<void(const std::string&)>, float, bool, bool
    )>
        fieldControl;
    std::function<void(
        const std::string&, const std::string&, const std::string&, std::vector<std::string>, std::size_t,
        std::function<void(std::size_t)>, bool
    )>
        chooseControl;
    std::function<
        void(const std::string&, const std::string&, const std::string&, bool, std::function<void(bool)>, bool)>
        toggleControl;
    std::function<void(
        const std::string&, const std::string&, const std::string&, double, double, double, double,
        std::function<void(double)>, const std::string&, bool
    )>
        sliderControl;

    UserSettingRow
    row(const std::string& title, const std::string& description = {}, float trailing = 0, bool stack = false) {
      return rowControl(title, description, trailing, stack);
    }
    UserSettingRow actionRow(const std::string& title, const std::string& description = {}) {
      return rowControl(title, description, 144, true);
    }
    void button(
        const std::string& caption, const std::string& action, ButtonVariant variant = ButtonVariant::Default,
        float buttonWidth = 128, bool enabled = true
    ) {
      buttonControl(node, 0, y, std::min(width, buttonWidth), caption, action, variant, enabled);
      y += 38;
    }
    void rowButton(
        UserSettingRow row, const std::string& caption, const std::string& action,
        ButtonVariant variant = ButtonVariant::Default, float buttonWidth = 128, bool enabled = true
    ) {
      buttonControl(
          node, width < 420 ? 0 : width - buttonWidth,
          width < 420 ? row.top + row.height - 38 : row.top + (row.height - 26) / 2, std::min(width, buttonWidth),
          caption, action, variant, enabled
      );
    }
    void field(
        const std::string& id, const std::string& title, const std::string& description, const std::string& initial,
        std::function<void(const std::string&)> changed, float height = 32, bool password = false, bool enabled = true
    ) {
      fieldControl(id, title, description, initial, std::move(changed), height, password, enabled);
    }
    void choose(
        const std::string& id, const std::string& title, const std::string& description,
        std::vector<std::string> choices, std::size_t selected, std::function<void(std::size_t)> changed,
        bool enabled = true
    ) {
      chooseControl(id, title, description, std::move(choices), selected, std::move(changed), enabled);
    }
    void toggle(
        const std::string& id, const std::string& title, const std::string& description, bool checked,
        std::function<void(bool)> changed, bool enabled = true
    ) {
      toggleControl(id, title, description, checked, std::move(changed), enabled);
    }
    void slider(
        const std::string& id, const std::string& title, const std::string& description, double value, double minimum,
        double maximum, double step, std::function<void(double)> changed, const std::string& unit = {},
        bool enabled = true
    ) {
      sliderControl(id, title, description, value, minimum, maximum, step, std::move(changed), unit, enabled);
    }
  };
} // namespace app
