#include "core/files/resource_paths.h"
#include "core/input/key_modifiers.h"
#include "core/input/keybind_matcher.h"
#include "core/text_clipboard.h"
#include "render/scene/input_area.h"
#include "render/scene/input_dispatcher.h"
#include "render/text/glyph_registry.h"
#include "ui/controls/input.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include "ui/controls/toggle.h"

#include <iostream>
#include <stdexcept>
#include <xkbcommon/xkbcommon-keysyms.h>

struct Clipboard final : TextClipboard {
  std::string text;
  std::optional<std::string> clipboardText() override { return text; }
  void setClipboardText(std::string value) override { text = std::move(value); }
};
int main() {
  auto require = [](bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  };
  try {
    FT_Library fonts = nullptr;
    require(FT_Init_FreeType(&fonts) == 0, "Initialize bundled icon font verification");
    FT_Face face = nullptr;
    const auto font = paths::assetPath("fonts/tabler-icons.ttf").string();
    require(FT_New_Face(fonts, font.c_str(), 0, &face) == 0, "Load the copied Tabler font");
    for (const auto* icon :
         {"message-circle", "settings", "users", "clipboard-plus", "mood-smile", "x", "minus", "square", "squares",
          "chevron-left", "chevron-right", "chevron-down", "zoom-in", "zoom-out", "loader"})
      require(
          GlyphRegistry::contains(icon) && FT_Get_Char_Index(face, GlyphRegistry::lookup(icon)) != 0,
          "Every native action glyph resolves to a bundled font character"
      );
    FT_Done_Face(face);
    FT_Done_FreeType(fonts);
    Clipboard clipboard;
    Input::setTextClipboard(&clipboard);
    Input::setValidateKeyMatcher([](std::uint32_t key, std::uint32_t) { return key == XKB_KEY_Return; });
    Input field;
    auto key = [&](std::uint32_t symbol, std::uint32_t modifiers = 0, std::uint32_t unicode = 0) {
      field.inputArea()->dispatchKey(symbol, unicode, modifiers, true);
    };
    field.setValue("one two");
    key(XKB_KEY_a, KeyMod::Ctrl);
    key(XKB_KEY_c, KeyMod::Ctrl);
    require(clipboard.text == "one two", "Native inputs implement Ctrl+A and Ctrl+C");
    key(XKB_KEY_x, KeyMod::Ctrl);
    require(field.value().empty(), "Native inputs cut the selection");
    key(XKB_KEY_z, KeyMod::Ctrl);
    require(field.value() == "one two", "Native input undo restores a cut");
    key(XKB_KEY_z, KeyMod::Ctrl | KeyMod::Shift);
    require(field.value().empty(), "Native input redo restores the cut");
    key(XKB_KEY_v, KeyMod::Ctrl);
    require(field.value() == "one two", "Native input paste uses its clipboard adapter");
    key(XKB_KEY_BackSpace, KeyMod::Ctrl);
    require(field.value() == "one ", "Ctrl+Backspace deletes the previous word");
    key(XKB_KEY_Home);
    key(XKB_KEY_Right, KeyMod::Ctrl | KeyMod::Shift);
    key(XKB_KEY_c, KeyMod::Ctrl);
    require(clipboard.text == "one ", "Ctrl+Shift+Arrow selects words");
    field.setValue("hello 👩‍💻");
    key(XKB_KEY_BackSpace);
    require(field.value() == "hello ", "Native composer deletes complete graphemes and joined emoji");
    field.setMultiline(true);
    field.setSubmitOnEnter(true);
    field.setValue("first");
    int submitted = 0;
    field.setOnSubmit([&](const std::string&) { ++submitted; });
    key(XKB_KEY_Return, KeyMod::Shift);
    require(field.value() == "first\n" && submitted == 0, "Shift+Enter inserts a line in chat mode");
    key(XKB_KEY_Return);
    require(submitted == 1 && field.value() == "first\n", "Enter submits a multiline message once");
    field.setMultiline(false);
    field.setPasswordMode(true);
    field.setValue("synthetic-secret");
    clipboard.text = "previous";
    key(XKB_KEY_a, KeyMod::Ctrl);
    key(XKB_KEY_c, KeyMod::Ctrl);
    require(clipboard.text == "previous", "Password inputs do not expose their selection through copy");
    Input::setTextClipboard(nullptr);
    KeybindMatcher::setMatcher(KeybindAction::Validate, [](std::uint32_t key, std::uint32_t modifiers) {
      return key == XKB_KEY_space && modifiers == 0;
    });
    Toggle toggle;
    bool changed = false;
    toggle.setOnChange([&](bool value) { changed = value; });
    auto* area = dynamic_cast<InputArea*>(toggle.children().back().get());
    require(area != nullptr, "Toggle owns a native input area");
    area->dispatchKey(XKB_KEY_space, ' ', 0, true);
    require(changed && toggle.checked(), "Toggle supports keyboard activation");
    toggle.setEnabled(false);
    changed = false;
    area->dispatchKey(XKB_KEY_space, ' ', 0, true);
    require(!changed && toggle.checked(), "Disabled account toggles cannot mutate values");
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
