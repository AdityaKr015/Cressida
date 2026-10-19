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
    field.setTextPresenter([](std::string_view text) {
      std::vector<TextReplacement> result;
      if (const auto at = text.find("<@123>"); at != std::string_view::npos)
        result.push_back({at, at + std::string_view("<@123>").size(), "@Maya"});
      return result;
    });
    field.setValue("Hello <@123>!");
    require(field.displayValue() == "Hello @Maya!", "Inputs present readable text while retaining protocol syntax");
    field.moveCaretLeft();
    field.moveCaretLeft();
    require(field.textInputState().cursor == 6, "Caret navigation treats a presented mention as one editing unit");
    field.moveCaretRight();
    key(XKB_KEY_BackSpace);
    require(field.value() == "Hello !", "Backspace removes a complete mention instead of corrupting its ID");
    key(XKB_KEY_z, KeyMod::Ctrl);
    require(
        field.value() == "Hello <@123>!" && field.displayValue() == "Hello @Maya!",
        "Undo restores mention source and presentation together"
    );
    key(XKB_KEY_a, KeyMod::Ctrl);
    key(XKB_KEY_c, KeyMod::Ctrl);
    require(clipboard.text == "Hello <@123>!", "Copy preserves the original mention identity");
    TextInputEdit preedit;
    preedit.hasPreedit = true;
    preedit.preeditText = "日本";
    field.clearSelection();
    field.textInputApplyEdit(preedit);
    require(
        field.textInputState().surroundingText == "Hello <@123>!", "IME surrounding text retains source byte offsets"
    );
    field.textInputResetPreedit();
    field.setValue("<@123>");
    TextInputEdit eraseMention;
    eraseMention.hasDelete = true;
    eraseMention.deleteBeforeLength = 1;
    field.textInputApplyEdit(eraseMention);
    require(field.value().empty(), "IME deletion expands to the complete displayed mention");
    key(XKB_KEY_z, KeyMod::Ctrl);
    require(
        field.value() == "<@123>" && field.displayValue() == "@Maya", "IME deletion remains one undoable source edit"
    );
    key(XKB_KEY_Home);
    TextInputEdit insertBefore;
    insertBefore.hasDelete = insertBefore.hasCommitText = true;
    insertBefore.commitText = "前 ";
    field.textInputApplyEdit(insertBefore);
    require(
        field.value() == "前 <@123>" && field.displayValue() == "前 @Maya",
        "A zero-length IME delete does not remove the following mention"
    );
    field.setTextPresenter({});
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
