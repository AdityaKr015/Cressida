#pragma once

#include <string>
#include <string_view>

// Cressida currently presents its native controls in English. Keep the copied
// controls' label lookup separate from rendering and from application state.
namespace i18n {
  inline std::string tr(std::string_view key) {
    if (key == "ui.controls.select.placeholder")
      return "Select…";
    if (key == "ui.dialogs.color-picker.title")
      return "Choose color";
    if (key == "common.actions.cancel")
      return "Cancel";
    if (key == "common.actions.apply")
      return "Apply";
    return std::string(key);
  }
} // namespace i18n
